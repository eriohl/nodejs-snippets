//
// The purpose of this exercise is to find a good way to wrap a third party
// object native object that needs to be garbage collected but which you only
// have a handle (or pointer) to and can't embed in an object wrap.
//
// The problem then with embedding the handle in the object wrap is that in
// order to get the pointer you need first get the object wrap, which you don't
// actually need to anything other then reading the pointer.
//
// This design stores the handle to both in the object wrap (referred to as the
// tracker) and the V8 object. When you then invoke a method on the object the
// implementation reads the handle of the underlying "third party" object
// directly from the V8 object rather then going through the object wrap.
// Reducing the number of memory hops by 1.
//

#include <assert.h>
#include <node.h>
#include <node_object_wrap.h>

#include "third_party.hpp"

namespace native_wrap {

class ThirdParty {
 public:
  static void Init(v8::Isolate* isolate) {
    // Prepare template.
    //
    // We create two internal fields: One for the tracker (field 0) and one
    // for the handle to the "third party" object (field 1).

    v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate, 0);
    tpl->SetClassName(
        v8::String::NewFromUtf8(isolate, "ThirdParty").ToLocalChecked());
    tpl->InstanceTemplate()->SetInternalFieldCount(2);

    // Register methods.

    NODE_SET_PROTOTYPE_METHOD(tpl, "plusOne", PlusOne);

    // Initialize instance factory.

    factory.Reset(isolate, tpl);

    // Create a new instance in order to get a hold of the prototype (which is
    // later used for type checking).

    v8::Local<v8::Object> instance =
        tpl->InstanceTemplate()
            ->NewInstance(isolate->GetCurrentContext())
            .ToLocalChecked();
    prototype.Reset(isolate, instance->GetPrototype());
  }

  static v8::Local<v8::Object> NewInstance(v8::Isolate* isolate,
                                           const double value) {
    // Create the "third party" object and get a handle to it.

    third_party::handle_t obj = third_party::create(value);

    // Create the V8 object to represent the "third party" object.

    v8::Local<v8::FunctionTemplate> tpl =
        v8::Local<v8::FunctionTemplate>::New(isolate, factory);
    v8::Local<v8::Object> handle =
        tpl->InstanceTemplate()
            ->NewInstance(isolate->GetCurrentContext())
            .ToLocalChecked();

    // Store the handle of the "third party" object in an internal field of
    // the V8 object.

    handle->SetAlignedPointerInInternalField(1, obj);

    // Create a tracker that destroys the third party object once it goes out
    // of scope.

    Tracker::New(obj, handle);

    return handle;
  }

  static bool IsInstanceOrThrow(v8::Isolate* isolate,
                                v8::MaybeLocal<v8::Object>& maybe_handle,
                                v8::Local<v8::Object>& handle) {
    if (maybe_handle.ToLocal(&handle)) {
      v8::Local<v8::Value> target_prototype = handle->GetPrototype();
      if (target_prototype == prototype) return true;
    }

    isolate->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(isolate, "<this> is not a ThirdParty")
            .ToLocalChecked()));
    return false;
  }

  static double PlusOne(v8::Local<v8::Object>& verified_handle) {
    third_party::handle_t obj = Unwrap(verified_handle);
    return third_party::plus_one(obj);
  }

 private:
  class Tracker : public node::ObjectWrap {
   public:
    ~Tracker() { third_party::destroy(obj_); }

    static void New(third_party::handle_t obj, v8::Local<v8::Object>& handle) {
      Tracker* t = new Tracker(obj);

      // What node::ObjectWrap::Wrap does is that is creates a weak peristent
      // handle referencing the V8 object. When all non-weak handled
      // referecing the object goes out of scope a callback registered with
      // weak handle is called which in turn deletes it. This is how the
      // destructor of the native object is called.

      t->Wrap(handle);
    }

   private:
    explicit Tracker(third_party::handle_t obj) : obj_(obj) {}

    third_party::handle_t obj_;
  };

  static third_party::handle_t Unwrap(v8::Local<v8::Object>& handle) {
    third_party::handle_t obj = handle->GetAlignedPointerFromInternalField(1);
    return obj;
  }

  static void PlusOne(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Object> this_handle = args.Holder();
    args.GetReturnValue().Set(v8::Number::New(isolate, PlusOne(this_handle)));
  }

  static v8::Persistent<v8::FunctionTemplate> factory;
  static v8::Persistent<v8::Value> prototype;
};

v8::Persistent<v8::FunctionTemplate> ThirdParty::factory;
v8::Persistent<v8::Value> ThirdParty::prototype;

static void CreateObject(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();

  double value =
      args[0]->IsUndefined()
          ? 0
          : args[0]->NumberValue(isolate->GetCurrentContext()).ToChecked();

  args.GetReturnValue().Set(ThirdParty::NewInstance(isolate, value));
}

static void PlusOne(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();

  if (args.Length() != 1) {
    isolate->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(isolate, "Wrong number of arguments")
            .ToLocalChecked()));
    return;
  }

  v8::MaybeLocal<v8::Object> arg_handle =
      args[0]->ToObject(isolate->GetCurrentContext());
  v8::Local<v8::Object> handle;

  if (!ThirdParty::IsInstanceOrThrow(isolate, arg_handle, handle)) return;

  args.GetReturnValue().Set(
      v8::Number::New(isolate, ThirdParty::PlusOne(handle)));
}

static void init(v8::Local<v8::Object> exports, v8::Local<v8::Value> module,
                 v8::Local<v8::Context> context) {
  ThirdParty::Init(exports->GetIsolate());

  NODE_SET_METHOD(exports, "createObject", CreateObject);
  NODE_SET_METHOD(exports, "plusOne", PlusOne);
}

}  // namespace native_wrap

extern "C" NODE_MODULE_EXPORT void NODE_MODULE_INITIALIZER(
    v8::Local<v8::Object> exports, v8::Local<v8::Value> module,
    v8::Local<v8::Context> context) {
  native_wrap::init(exports, module, context);
}
