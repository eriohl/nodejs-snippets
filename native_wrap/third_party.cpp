#include "third_party.hpp"

#include <stdio.h>

namespace third_party {

class Object {
  double value_;

 public:
  Object(double value) : value_(value) {}

  double PlusOne() { return ++value_; }
};

handle_t create(double value) {
  Object *obj = new Object(value);
  return obj;
}

void destroy(handle_t handle) {
  printf("third_party::destroy\n");

  Object *obj = reinterpret_cast<Object *>(handle);
  delete obj;
}

double plus_one(handle_t handle) {
  Object *obj = reinterpret_cast<Object *>(handle);
  return obj->PlusOne();
}

}  // namespace third_party
