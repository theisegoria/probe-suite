// cvar.h
//
// Console variables. Registered by construction, read through Get().
//
// A cvar left at its declared default is indistinguishable at runtime from
// one that was explicitly set to the same value, so code that needs to know
// whether a value came from configuration compares against Default().
//
// Copyright (c) Northlight Interactive. Internal core header.

#pragma once

#include <cstdint>

namespace nl {
namespace core {

template <typename T>
class CVar {
public:
    CVar(const char* name, T default_value, const char* help)
        : name_(name), value_(default_value), default_(default_value), help_(help) {
        Register(this);
    }

    T Get() const { return value_; }
    void Set(T v) { value_ = v; }
    T Default() const { return default_; }
    bool IsDefault() const { return value_ == default_; }

    const char* Name() const { return name_; }
    const char* Help() const { return help_; }

private:
    static void Register(CVar* self);

    const char* name_;
    T value_;
    T default_;
    const char* help_;
};

// Applies a configuration file over the registered cvars. Values that are not
// mentioned in the file keep the default they were declared with.
void ApplyConfigFile(const char* path);

}  // namespace core
}  // namespace nl
