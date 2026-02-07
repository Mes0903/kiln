macro(my_macro)
  message("inside macro, about to return")
  return()
  message("after return in macro - should NOT print")
endmacro()

message("in included file, calling macro")
my_macro()
message("after macro in included file - should NOT print")
