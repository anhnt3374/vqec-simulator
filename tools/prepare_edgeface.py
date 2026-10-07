#!/usr/bin/env python3
"""Run in Ubuntu Docker with torch 2.2.2, timm 0.9.12, onnx 1.16.1; original GELU."""
import argparse
import hashlib
import json
import sys
from pathlib import Path
import numpy as np
import onnx
import onnxruntime as ort
import torch
p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True)
p.add_argument('--checkpoint', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
checksum = hashlib.sha256(a.checkpoint.read_bytes()).hexdigest()
if checksum != 'dc59abda2e8580399fd115a1eeb07e1f21156196db604b884407bcf0f17efb07':
    raise ValueError('wrong official EdgeFace checkpoint')
sys.path.insert(0, str(a.source))
from backbones import get_model
model = get_model('edgeface_s_gamma_05')
model.load_state_dict(torch.load(a.checkpoint, map_location='cpu', weights_only=True), strict=True)
model.eval()
torch.set_num_threads(2)
a.output.parent.mkdir(parents=True, exist_ok=True)
torch.onnx.export(model, torch.zeros(4, 3, 112, 112), str(a.output), input_names=['input'],
                  output_names=['embedding'], opset_version=17, do_constant_folding=True)
onnx.checker.check_model(onnx.load(a.output))
session = ort.InferenceSession(str(a.output), providers=['CPUExecutionProvider'])
rng = np.random.default_rng(42)
for count in range(1, 5):
    inputs = np.zeros((4, 3, 112, 112), dtype=np.float32)
    inputs[:count] = rng.uniform(-1, 1, inputs[:count].shape)
    actual = session.run(None, {'input': inputs})[0]
    assert actual.shape == (4, 512) and np.isfinite(actual).all()
    for i in range(count):
        with torch.no_grad():
            expected = model(torch.from_numpy(inputs[i:i+1])).numpy()[0]
        np.testing.assert_allclose(actual[i], expected, atol=2e-4, rtol=2e-4)
metadata = {'model_id': 'edgeface_s_gamma_05_v1', 'checkpoint_sha256': checksum,
            'artifact_sha256': hashlib.sha256(a.output.read_bytes()).hexdigest(),
            'input': [4, 3, 112, 112], 'output': [4, 512], 'verified_counts': [1, 2, 3, 4],
            'torch': torch.__version__, 'onnx': onnx.__version__, 'ort': ort.__version__}
a.output.with_suffix('.metadata.json').write_text(json.dumps(metadata, indent=2)+'\n')
print(json.dumps(metadata))
