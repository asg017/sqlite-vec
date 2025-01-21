export ECR_REPOSITORY=nodemodules/sql-lite-vec # created and changed
export DOCKER_REPOSITORY=h4ckermike/sql-lite-vec # created and changed
export SESSION_APP_NAME=sql-lite-vec # we need this
export AWS_ACCOUNT=767503528736
export AWS_REGION=us-east-2
export TAG=latest

aws ecr get-login-password --region ${AWS_REGION} | docker login --username AWS --password-stdin ${AWS_ACCOUNT}.dkr.ecr.${AWS_REGION}.amazonaws.com
docker pull ${AWS_ACCOUNT}.dkr.ecr.${AWS_REGION}.amazonaws.com/${ECR_REPOSITORY}
docker tag ${AWS_ACCOUNT}.dkr.ecr.${AWS_REGION}.amazonaws.com/${ECR_REPOSITORY}:${TAG} ${DOCKER_REPOSITORY}:${TAG}
docker push ${DOCKER_REPOSITORY}:${TAG}
