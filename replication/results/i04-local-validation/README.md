# Local smoke validation

The emulated Linux x86-64 container built Silo and produced one five-second stock TPC-C measurement at each worker count. The debug test run reached the fast upstream checks before its diagnostic timeout (exit 124). A separate, longer local attempt was killed under container memory pressure. The CSV is retained as functional evidence. Use the native PACE results for performance reporting.
