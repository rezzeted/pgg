#pragma once

// CPU smoke for PggServe (--smoke): load/probe/docs/slots and two TCP
// clients probing different files concurrently. No Sokol / GPU.
bool runPggServeSmokeTest();
