# Typed business services use the public ATOP API

Status: accepted.

Role selection is needed by multiple products. It has business semantics beyond
HTTP: editing templates creates and binds custom roles, edits omit unchanged
fields, language/timbre lists use full language codes, and results have several
known list envelopes. This knowledge belongs in shared named wrappers.

Place these wrappers in `modules/biz-client`, above `iot_atop_call()`. Keep IoT
activation, credentials and ATOP transport in iot-client, and Session/Event and
media transport in rtc-tcp-client. A Business Client borrows an IoT Client and
its PAL; callers never supply cloud secrets again.

Use typed C results, explicit pagination and matching release functions. Keep
cloud rejection details, validate results, and reject truncation. Use PAL
allocation and the common log facade so POSIX and FreeRTOS share implementation.
Add sources to both root CMake and the ESP-IDF component.

NVS caching, workers, queue ownership, avatar HTTP fetching/decoding, LVGL and
TAI reconnection remain application concerns. Copying the study-machine role
component unchanged would import ESP-IDF/PSRAM/NVS and a global client into a
portable SDK, so only its cloud API semantics are carried over.
