# Convert old message store files to new ones, or split new ones back up into old ones.

import os
import sys

def makedirs(path):
    if not os.path.isdir(path):
        os.makedirs(path)

def py2tp(s):
    """Convert a string to a textparser-happy representation."""
    if "<&" not in s and "&>" not in s:
        return "<&%s&>" % s
    elif "<<" not in s and ">>" not in s:
        return "<<&%s&>>" % s
    else:
        raise SystemExit("Unable to escape string %r" % s)

def tp2py(s):
    """Read a textparser string to a normal Python one."""
    s = s.strip()
    if s.startswith("<&") or s.startswith("<<"):
        return s[2:-2]
    elif s.startswith('"'):
        return s[1:-1].decode("string_escape")
    else:
        return s

def py2ms(s):
    """Convert a Python string to an old-style message."""
    return "%s" % s.replace('"', '\\"')

# Old message strings are valid Python strings already.
ms2py = eval

class Message(object):
    key = ""
    scope = ""
    description = ""
    translation = ""

    def __init__(self, **kwargs):
        self.__dict__.update(**kwargs)
        if not self.key: raise SystemExit("Messages must have keys")
        self.key = self.key.replace("/", "---").title() # canonically store new format keys
        if not self.scope: self.scope = "MungedMessage/" + self.key
        if not self.translation: self.translation = self.key
        if not self.description: self.description = "Munged translation for %r" % self.key
        self.scope = self.scope.title()

    def old_format(self):
        return '"%s", "%s"' % (self.key.replace("---", "/"), self.translation.replace('"', '\\"'))

    def new_format(self):
        return """\
Message
{
    MessageKey %(key)s
    Scope %(scope)s
    Description %(description)s
    DefaultString %(translation)s
}

""" % dict(key=self.key, scope=self.scope, description=py2tp(self.description),
           translation=py2tp(self.translation))

def old_to_new(argv):
    try:
        scope, output = argv[:2]
        input = argv[2:] 
    except (TypeError, ValueError):
        raise SystemExit("Parameters: scope_prefix output_file input_folder ...")

    # read in all old messages
    messages = {}
    for input_path in input:
        for root, dirnames, filenames in os.walk(input_path):
            for filename in filenames:
                if filename.lower().endswith(".ms"):
                    path = os.path.join(root, filename)
                    for line in open(path, "rU"):
                        line = line.strip()
                        line = line.strip("\xef\xbb\xbf") # strip UTF8 BOM, I hate Windows
                        if not line or line[0] == "#" or line[0:2] == "//": continue
                        elif line[0] == '"':
                            try: key, value = eval(line)
                            except: pass
                            else:
                                messages[key] = Message(key=key, translation=value, scope=scope)
                                continue
                        print "Unknown line %r in %s, skipping" % (line, path)

    # print out messages in new format
    if os.path.dirname(output):
        makedirs(os.path.dirname(output))
    try: os.chmod(output, 0666)
    except: pass
    out = open(output, "wt")
    print "Writing to", out.name
    for message in sorted(messages.values(), key=lambda m: m.key):
        print >>out, message.new_format()
    out.close()
    print "Done."

def new_to_old(argv):
    try:
        scope, output_path, output_file = argv[:3]
        input = argv[3:] 
    except (TypeError, ValueError):
        raise SystemExit("Parameters: scope_prefix output_folder output_file input_folder ...")

    loaded = {}
    found = {}
    scope = scope.lower()

    for input_path in input:
        for root, dirnames, filenames in os.walk(input_path):
            for filename in filenames:
                if filename.lower().endswith(".translation"):
                    language = filename.rsplit(".", 2)[1].lower()
                    print "Found language", language
                    print "Scanning", filename
                    for line in file(os.path.join(root, filename), "rU"):
                        line = line.strip()
                        if line.lower().rstrip("{") == "message" or line == "}": # start of a new message
                            if found.get("scope", "").lower().startswith(scope):
                                m = Message(key=found.get("messagekey", ""), translation=found.get("translatedstring", found.get("defaultstring", "")))
                                loaded.setdefault(language, []).append(m)
                            found = {} # reset loaded information
                        try: field, value = line.split(None, 1)
                        except: continue
                        else: found[field.lower()] = tp2py(value)

    for language, messages in loaded.iteritems():
        path = os.path.join(output_path, language, output_file)
        makedirs(os.path.dirname(path))
        try: os.chmod(path, 0666)
	except WindowsError, err:
		print "Unable to chmod, may encounter permission problems: " + str(err)
        out = file(path, "wt")
        print "Writing", len(messages), "to", out.name
        for message in messages:
            print >>out, message.old_format()
        out.close()

if __name__ == "__main__":
    args = sys.argv[1:]
    
    actions = dict(old2new=old_to_new, new2old=new_to_old)

    try: action = actions[args.pop(0).lower()]
    except IndexError:
        raise SystemExit("Needs to be called with one of the following commands: %s" % (
            ", ".join(actions.keys())))
    else:
        action(args)
