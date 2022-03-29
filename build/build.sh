#!/bin/bash
release=${1:-1}

# Must be in the build directory
current_file="$(realpath ${0})"
work_path="$(dirname "${current_file}")"

pushd ${work_path}

# Output artifacts
if [[ -d ./out ]] ; then
	rm -rvf ./out
fi

mkdir -p ./out
chmod a+w ./out

work_path=$(dirname $work_path)
args=""

if [[ -n $https_proxy ]] ; then args="--build-arg PROXY=${https_proxy}" ; fi

docker build ${args} -t jers-build .
docker run -v "${work_path}:/jers:Z" -e RELEASE=${release} --user rpmbuild jers-build /jers/build/build_rpm.sh

popd
