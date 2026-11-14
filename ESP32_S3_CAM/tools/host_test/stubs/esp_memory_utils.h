#pragma once
inline bool esp_ptr_external_ram(const void*) { return false; }
inline bool esp_ptr_in_drom(const void*) { return true; }
