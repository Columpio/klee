rm -rf ~/klee_build/klee_build110z3/
./default_build.sh
rm -rf /tmp/klee-out/
/home/columpio/klee_build/klee_build110z3/bin/klee --use-guided-search=error --mock-external-calls --posix-runtime --check-out-of-memory --suppress-external-warnings       --libc=klee --skip-not-lazy-initialized --output-source=false --output-istats=false --output-stats=false       --output-dir=/tmp/klee-out --max-time=240s --max-solver-time=5s       --mock-all-externals --smart-resolve-entry-function --extern-calls-can-return-null --fork-partial-validity       --align-symbolic-pointers=false --use-query-log=all:smt2 --analysis-reproduce=/home/columpio/tmp/evaluation/flite/2_without_first_event_with_return.sarif /home/columpio/tmp/evaluation/flite/input.bc &> ~/Desktop/test1.log
python analyze_hard_queries.py > tmp.log
