"""Merges a decoder without past key values and one with them into the "merged" decoder the app runs (as Optimum's
merge_decoders made the Xenova export): one graph whose `If` on the boolean input `use_cache_branch` runs either.

- inputs: input_ids, encoder_hidden_states, past_key_values.{i}.{decoder,encoder}.{key,value}, use_cache_branch;
- outputs: logits, present.{i}.{decoder,encoder}.{key,value} (the no-past graph's outputs, in its order). In the
  with-past branch the encoder's present.* are its past inputs passed through (the app keeps the first step's).
Weights that both graphs hold with the same bytes are stored once, in the outer graph.
"""
from __future__ import annotations

import hashlib

import onnx
from onnx import helper


def _rename_graph(g: onnx.GraphProto, prefix: str, keep: set[str], init_map: dict[str, str]):
    """Renames every value made inside `g` (node outputs) with `prefix`; references to initializers go to their merged
    names; names in `keep` (the outer inputs) stay."""
    made = {o for n in g.node for o in n.output if o}
    ren = {n: prefix + n for n in made}
    ren.update(init_map)

    def fix(name):
        return ren.get(name, name) if name not in keep or name in init_map else name

    for n in g.node:
        n.input[:] = [fix(x) if x else x for x in n.input]
        n.output[:] = [ren.get(x, x) if x else x for x in n.output]
        if n.name:
            n.name = prefix + n.name
        for a in n.attribute:
            if a.type == onnx.AttributeProto.GRAPH:
                _rename_sub(a.g, ren)
            elif a.type == onnx.AttributeProto.GRAPHS:
                for sg in a.graphs:
                    _rename_sub(sg, ren)
    for o in g.output:
        o.name = fix(o.name)
    return ren


def _rename_sub(g: onnx.GraphProto, ren: dict):
    for n in g.node:
        n.input[:] = [ren.get(x, x) for x in n.input]


def _dedupe_initializers(graphs: list[onnx.GraphProto]):
    """One merged name per distinct tensor; returns (initializers, [map old -> new per graph])."""
    by_hash: dict[str, onnx.TensorProto] = {}
    maps = []
    for g in graphs:
        m = {}
        for t in g.initializer:
            anon = onnx.TensorProto()
            anon.CopyFrom(t)
            anon.name = ""
            h = hashlib.sha256(anon.SerializeToString()).hexdigest()
            key = f"{t.data_type}:{list(t.dims)}:{h}"
            if key not in by_hash:
                nt = onnx.TensorProto()
                nt.CopyFrom(t)
                nt.name = f"shared_{len(by_hash)}_{t.name}"[:200]
                by_hash[key] = nt
            m[t.name] = by_hash[key].name
        maps.append(m)
    return list(by_hash.values()), maps


def merge(no_past: onnx.ModelProto, with_past: onnx.ModelProto) -> onnx.ModelProto:
    a, b = onnx.ModelProto(), onnx.ModelProto()
    a.CopyFrom(no_past)
    b.CopyFrom(with_past)
    ga, gb = a.graph, b.graph
    inits, (ma, mb) = _dedupe_initializers([ga, gb])
    outer_inputs = []
    seen = set()
    for vi in list(ga.input) + list(gb.input):
        if vi.name not in seen and vi.name not in ma and vi.name not in mb:
            seen.add(vi.name)
            outer_inputs.append(vi)
    # encoder_hidden_states may have been dropped from the with-past graph (unused): it is an outer input anyway
    keep = {vi.name for vi in outer_inputs}
    # the with-past branch passes the encoder's cache through
    b_outs = {o.name for o in gb.output}
    for o in ga.output:
        if o.name not in b_outs:
            src = o.name.replace("present", "past_key_values", 1)
            if src not in keep:
                raise ValueError(f"with-past graph lacks {o.name} and the input {src}")
            gb.node.append(helper.make_node("Identity", [src], [o.name], name=f"pass_{o.name}"))
    order = [o.name for o in ga.output]
    gb_out = {o.name: o for o in gb.output}
    for o in ga.output:
        if o.name not in gb_out:
            vi = onnx.ValueInfoProto()
            vi.CopyFrom(o)
            gb_out[o.name] = vi
    del gb.output[:]
    gb.output.extend(gb_out[n] for n in order)
    _rename_graph(ga, "no_past/", keep, ma)
    _rename_graph(gb, "with_past/", keep, mb)
    for g in (ga, gb):
        del g.initializer[:]
        del g.input[:]
        del g.value_info[:]
    then_g = helper.make_graph(list(gb.node), "with_past", [], list(gb.output))
    else_g = helper.make_graph(list(ga.node), "no_past", [], list(ga.output))
    use_cache = helper.make_tensor_value_info("use_cache_branch", onnx.TensorProto.BOOL, [1])
    outs = []
    for name in order:
        o = next(x for x in no_past.graph.output if x.name == name)
        vi = onnx.ValueInfoProto()
        vi.CopyFrom(o)
        outs.append(vi)
    node = helper.make_node("If", ["use_cache_branch"], order, name="optimum::if", then_branch=then_g,
                            else_branch=else_g)
    graph = helper.make_graph([node], "merged_decoder", outer_inputs + [use_cache], outs, initializer=inits)
    opsets = {o.domain: o.version for o in list(a.opset_import) + list(b.opset_import)}
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid(d, v) for d, v in opsets.items()],
                              producer_name="xqt_hwr")
    model.ir_version = max(a.ir_version, b.ir_version)
    onnx.checker.check_model(model)
    return model
