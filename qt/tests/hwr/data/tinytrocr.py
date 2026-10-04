# Generates tiny-trocr/ (next to this script; needs pip install onnx): a stand-in for TrOCR with the interface of
# Xenova's export (encoder: pixel_values -> last_hidden_state; merged decoder: input_ids, encoder_hidden_states,
# use_cache_branch, past_key_values.0.decoder.key -> logits, present.0.decoder.key) whose next token depends only on
# the last one: <s> -> a (or c), a -> b, b -> </s>, c -> </s>. For qt/tests/hwr/TrocrTest.cpp (with XQT_ONNXRUNTIME).
import json, os
import onnx
from onnx import helper, TensorProto

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tiny-trocr")
os.makedirs(os.path.join(out, "onnx"), exist_ok=True)
V = 5  # <s> a b c </s>

# Encoder
px = helper.make_tensor_value_info("pixel_values", TensorProto.FLOAT, [1, 3, 384, 384])
hs = helper.make_tensor_value_info("last_hidden_state", TensorProto.FLOAT, [1, 1, 4])
enc = helper.make_graph([
    helper.make_node("ReduceMean", ["pixel_values"], ["m"], keepdims=1),
    helper.make_node("Reshape", ["m", "s111"], ["m3"]),
    helper.make_node("Expand", ["m3", "s114"], ["last_hidden_state"]),
], "encoder", [px], [hs], [helper.make_tensor("s111", TensorProto.INT64, [3], [1, 1, 1]),
                          helper.make_tensor("s114", TensorProto.INT64, [3], [1, 1, 4])])
m = helper.make_model(enc, opset_imports=[helper.make_opsetid("", 13)], producer_name="xournal-qt tests")
m.ir_version = 8
onnx.checker.check_model(m)
onnx.save(m, os.path.join(out, "onnx", "encoder.onnx"))

# Decoder: logits = T[last token]
T = [[-9.0] * V for _ in range(V)]
T[0][1], T[0][3] = 2.0, 1.5
T[1][2] = 3.0
T[2][4] = 3.0
T[3][4] = 3.0
T[4][4] = 3.0
ids = helper.make_tensor_value_info("input_ids", TensorProto.INT64, ["b", 1])
ehs = helper.make_tensor_value_info("encoder_hidden_states", TensorProto.FLOAT, ["b", "n", 4])
branch = helper.make_tensor_value_info("use_cache_branch", TensorProto.BOOL, [1])
past = helper.make_tensor_value_info("past_key_values.0.decoder.key", TensorProto.FLOAT, ["b", 2, "s", 4])
logits = helper.make_tensor_value_info("logits", TensorProto.FLOAT, ["b", 1, V])
present = helper.make_tensor_value_info("present.0.decoder.key", TensorProto.FLOAT, ["b", 2, "s1", 4])
dec = helper.make_graph([
    helper.make_node("Gather", ["table", "input_ids"], ["logits"], axis=0),
    helper.make_node("Cast", ["input_ids"], ["idf"], to=TensorProto.FLOAT),
    helper.make_node("Reshape", ["idf", "s1111"], ["id4"]),
    helper.make_node("Expand", ["id4", "s1214"], ["new"]),
    helper.make_node("Concat", ["past_key_values.0.decoder.key", "new"], ["present.0.decoder.key"], axis=2),
], "decoder", [ids, ehs, branch, past], [logits, present],
    [helper.make_tensor("table", TensorProto.FLOAT, [V, V], [v for row in T for v in row]),
     helper.make_tensor("s1111", TensorProto.INT64, [4], [-1, 1, 1, 1]),
     helper.make_tensor("s1214", TensorProto.INT64, [4], [1, 2, 1, 4])])
m = helper.make_model(dec, opset_imports=[helper.make_opsetid("", 13)], producer_name="xournal-qt tests")
m.ir_version = 8
onnx.checker.check_model(m)
onnx.save(m, os.path.join(out, "onnx", "decoder.onnx"))

tok = {"model": {"type": "Unigram", "vocab": [["<s>", 0], ["▁a", 0], ["▁b", 0], ["▁c", 0], ["</s>", 0]]},
       "decoder": {"type": "Metaspace"},
       "added_tokens": [{"id": 0, "content": "<s>", "special": True}, {"id": 4, "content": "</s>", "special": True}]}
with open(os.path.join(out, "tokenizer.json"), "w") as f:
    json.dump(tok, f)
