# This file tests return() from an included file
message("Included file: start")
return()
message("Included file: after return - SHOULD NOT PRINT")
