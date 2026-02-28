# Round-Trip Test Commands

Single tests:

1. `ctest -C Debug --test-dir cmake-build-debug -R test_round_trip --output-on-failure`
2. `ctest -C Debug --test-dir cmake-build-debug -R test_real_file_roundtrip --output-on-failure`
3. `ctest -C Debug --test-dir cmake-build-debug -R test_behavior_roundtrip_regression --output-on-failure`
4. `ctest -C Debug --test-dir cmake-build-debug -R test_file_roundtrip --output-on-failure`
5. `ctest -C Debug --test-dir cmake-build-debug -R test_data_roundtrip --output-on-failure`
6. `ctest -C Debug --test-dir cmake-build-debug -R test_load_save_mmap_baseline --output-on-failure`

Batch run:

1. `ctest -C Debug --test-dir cmake-build-debug -R "(round_trip|roundtrip|file_roundtrip|data_roundtrip)" --output-on-failure`
