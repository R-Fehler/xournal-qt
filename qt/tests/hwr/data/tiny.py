# Generates tiny.onnx (next to this script; needs pip install onnx): y = 2 x + float(ids); z = flag ? x : 0, a smoke
# test of the ONNX Runtime calls (qt/tests/hwr/TrocrTest.cpp)
import onnx
from onnx import helper, TensorProto
x = helper.make_tensor_value_info("x", TensorProto.FLOAT, ["n", 3])
ids = helper.make_tensor_value_info("ids", TensorProto.INT64, ["n", 1])
flag = helper.make_tensor_value_info("flag", TensorProto.BOOL, [1])
y = helper.make_tensor_value_info("y", TensorProto.FLOAT, ["n", 3])
z = helper.make_tensor_value_info("z", TensorProto.FLOAT, ["n", 3])
two = helper.make_tensor("two", TensorProto.FLOAT, [1], [2.0])
zero = helper.make_tensor("zero", TensorProto.FLOAT, [1], [0.0])
nodes = [
    helper.make_node("Mul", ["x", "two"], ["x2"]),
    helper.make_node("Cast", ["ids"], ["idsf"], to=TensorProto.FLOAT),
    helper.make_node("Add", ["x2", "idsf"], ["y"]),
    helper.make_node("Where", ["flag", "x", "zero"], ["z"]),
]
g = helper.make_graph(nodes, "tiny", [x, ids, flag], [y, z], [two, zero])
m = helper.make_model(g, opset_imports=[helper.make_opsetid("", 13)], producer_name="xournal-qt tests")
m.ir_version = 8
onnx.checker.check_model(m)
import os
onnx.save(m, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tiny.onnx"))
