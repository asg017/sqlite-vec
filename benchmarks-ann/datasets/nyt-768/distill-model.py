# /// script
# requires-python = ">=3.12"
# dependencies = [
#     "model2vec[distill]",
#     "torch<=2.7",
# ]
# ///

from model2vec.distill import distill

model = distill(model_name="BAAI/bge-base-en-v1.5", pca_dims=768)
model.save_pretrained("bge-base-en-v1.5-768")
print("Saved distilled model to bge-base-en-v1.5-768/")
