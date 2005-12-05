#!/usr/bin/python2.4
import telepathy.errors
import inspect 
import gengobject

out = open("telepathy-errors.h", 'w')

gengobject.print_license(out, "telepathy-errors.h", "Header for Telepathy error types")

gengobject.print_header_begin(out, "telepathy_errors")
out.write("typedef enum\n{\n")

errors = [];
max_name_length = 0
for (cname,val) in telepathy.errors.__dict__.items():
    if inspect.isclass(val):
        if '_dbus_error_name' in val.__dict__:
            errors.append(val)
            if len(val.__name__) > max_name_length:
                max_name_length = len(val.__name__)


for val in errors:
    line = "  "+val.__name__ + ","
    line = line.ljust(max_name_length + 4)
    line += "/** "+val.__doc__.strip()
    while len (line) > 79:
        cut = line[:80]
        cutidx = cut.rfind(' ')
        if cutidx == 80:
            out.write(cut[:79])
        else:
            out.write(cut[:cutidx]) 
        out.write('\n')
        line = ' ' * (max_name_length +4) + ' * ' + line[cutidx:]
    out.write(line + '\n')
    out.write(' ' * (max_name_length + 4) + ' */\n')

out.write("} TelepathyErrors; \n\n")

gengobject.print_header_end(out, "telepathy_errors");
