# Generates tiny-ctc/ (next to this script; needs pip install onnx): a stand-in for a CTC line model with the
# interface of qt/research/hwr/train/FORMATS.md ("kind": "ctc"): image float32 [1, 1, 16, W] -> logits [W, 1, 4]
# (log-softmax over blank, " ", "a", "b"). Per column x (the ink's mean over the height) and m (its mean over 9
# columns): "a" where there is ink, " " in the middle of a wide gap, else the blank; "b" never. So a line of two
# written words reads "a a". The input height is fixed (16): a picture of another height is refused by ONNX Runtime.
# For qt/tests/hwr/CtcTest.cpp (with XQT_ONNXRUNTIME).
import hashlib, json, os
import onnx
from onnx import helper, TensorProto

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tiny-ctc")
os.makedirs(out, exist_ok=True)
C = 4  # blank, " ", "a", "b"

image = helper.make_tensor_value_info("image", TensorProto.FLOAT, [1, 1, 16, "W"])
logits = helper.make_tensor_value_info("logits", TensorProto.FLOAT, ["W", 1, C])
# logits = [x, m] @ Wt + b
Wt = [0.0, 0.0, 40.0, 0.0,
      0.0, -200.0, 0.0, 0.0]
b = [1.0, 2.0, -1.0, -10.0]
g = helper.make_graph([
    helper.make_node("ReduceMean", ["image"], ["x4"], axes=[2], keepdims=0),          # [1, 1, W]
    helper.make_node("AveragePool", ["x4"], ["m"], kernel_shape=[9], pads=[4, 4],
                     count_include_pad=1),                                              # [1, 1, W]
    helper.make_node("Concat", ["x4", "m"], ["f"], axis=1),                            # [1, 2, W]
    helper.make_node("Transpose", ["f"], ["ft"], perm=[2, 0, 1]),                       # [W, 1, 2]
    helper.make_node("MatMul", ["ft", "Wt"], ["l0"]),                                   # [W, 1, C]
    helper.make_node("Add", ["l0", "b"], ["l1"]),
    helper.make_node("LogSoftmax", ["l1"], ["logits"], axis=2),
], "tinyctc", [image], [logits],
    [helper.make_tensor("Wt", TensorProto.FLOAT, [2, C], Wt), helper.make_tensor("b", TensorProto.FLOAT, [C], b)])
m = helper.make_model(g, opset_imports=[helper.make_opsetid("", 13)], producer_name="xournal-qt tests")
m.ir_version = 8
onnx.checker.check_model(m)
onnx.save(m, os.path.join(out, "model.onnx"))

with open(os.path.join(out, "alphabet.txt"), "w", encoding="utf-8") as f:
    f.write(" \na\nb\n")


def entry(name):
    data = open(os.path.join(out, name), "rb").read()
    return {"sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}


manifest = {"kind": "ctc", "name": "tiny-ctc", "languages": ["de"], "version": "test", "model": "model.onnx",
            "alphabet": "alphabet.txt", "blank": 0, "input_height": 16, "max_width": 2048, "licence": "GPL-2.0-or-later",
            "trained_on": [], "files": {"model.onnx": entry("model.onnx"), "alphabet.txt": entry("alphabet.txt")}}
with open(os.path.join(out, "model.json"), "w") as f:
    json.dump(manifest, f, indent=2)
    f.write("\n")
