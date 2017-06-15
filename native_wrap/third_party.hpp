
#pragma once


namespace third_party {
  
  
typedef void * handle_t;

extern handle_t create(double value);

extern void destroy(handle_t handle);

extern double plus_one(handle_t handle);

  
} // namespace third_party
