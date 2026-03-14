// RUN: split-file %s %t
// RUN: %clang_cc1 -load %llvmshlibdir/kutuzov_i_lab_1_ClangAST%pluginext -plugin no_override_warnings -fsyntax-only -verify %t/with_warnings.cpp
// RUN: %clang_cc1 -load %llvmshlibdir/kutuzov_i_lab_1_ClangAST%pluginext -plugin no_override_warnings -fsyntax-only -verify %t/without_warnings.cpp

//--- with_warnings.cpp

class Parent_Warn {
public:
    Parent_Warn() {}
    virtual ~Parent_Warn() {}

protected:
    virtual bool method() { 
        return false; 
    }
};

class Child_Warn: public Parent_Warn {
public:
    Child_Warn() {}
    virtual ~Child_Warn() {} // expected-warning {{method '~Child_Warn' overrides a virtual method but has no 'override' specifier!}}

protected:
    virtual bool method() { // expected-warning {{method 'method' overrides a virtual method but has no 'override' specifier!}}
        return true; 
    }
};


//--- without_warnings.cpp
// expected-no-diagnostics


class Parent_NoWarn {
public:
    Parent_NoWarn() {}
    virtual ~Parent_NoWarn() {}

protected:
    virtual bool method() { 
        return false; 
    }
};

class Child_NoWarn: public Parent_NoWarn {
public:
    Child_NoWarn() {}
    virtual ~Child_NoWarn() override {}

protected:
    virtual bool method() override {
        return true; 
    }
};