CUR_PATH=$(pwd)
PROJECT_PATH=$(cd "$(dirname "$0")/.." && pwd)
LOOP_NUM=${1:-5}

cd "${PROJECT_PATH}/build/test"

i=1
while [ "$i" -le "${LOOP_NUM}" ]; do
    echo "Run count = ${i}"
    ./thread_pool_test --gtest_filter='PerformanceBenchmark.*'
    i=$((i+1))
done
cd "${CUR_PATH}"