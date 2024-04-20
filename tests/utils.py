import numpy as np
from io import BytesIO


def to_npy(arr):
    buf = BytesIO()
    np.save(buf, arr)
    buf.seek(0)
    return buf.read()


to_npy(np.array([[1.0, 2.0, 3.0], [2.0, 3.0, 4.0]], dtype=np.float32))

print(to_npy(np.array([[1.0, 2.0]], dtype=np.float32)))
print(to_npy(np.array([1.0, 2.0], dtype=np.float32)))

to_npy(
    np.array(
        [np.zeros(10), np.zeros(10), np.zeros(10), np.zeros(10), np.zeros(10)],
        dtype=np.float32,
    )
)
