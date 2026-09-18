# Core unit tests

test/core project covering argument parsing, App stop conditions and the packet queue.

## Details

Four suites: `Args` covers every command line option and scene loading, `App` covers the frame, timeout and quit stop conditions, the process folder lifecycle and CPU budget pacing, `Queue` covers the packet ring including blocking behaviour across threads, and `Surface` runs headless frames and verifies the reported size and the PNG capture with and without frame suffixes.
