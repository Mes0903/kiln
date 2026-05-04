extern "C" int vendor_value();
extern "C" int my_value() { return vendor_value() + 1; }
