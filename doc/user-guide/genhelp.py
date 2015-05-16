import re
import xml.etree.ElementTree as ET

IN_FILE = 'help.xml'
OUT_FILE = 'help.txt'
NORMALIZE_RE = re.compile(r"([^<>\s\t])[\s\t]+([^<>\s\t])")

def join(list):
    return ''.join([str(x) for x in list])

def normalize(x):
    x = NORMALIZE_RE.sub(r"\1 \2", x or '')
    return x.replace("\n", "").replace("\t", "")

def fix_tree(tag, lvl=''):
    if tag.tag.count("XInclude"):
        tag.tag = 'include'

    #print("%s<%s>%r" % (lvl, tag.tag, [tag.text, normalize(tag.text)]))

    for subtag in tag:
        fix_tree(subtag, lvl + "  ")

    #print("%s</%s>%r" % (lvl, tag.tag, [tag.tail, normalize(tag.tail)]))

    tag.text = normalize(tag.text)
    tag.tail = normalize(tag.tail)

def parse_file(filename, parent=None):
    tree = ET.parse(open(filename)).getroot()
    fix_tree(tree)
    return parse_tag(tree, parent)

def parse_tag(tag, parent):
    fun = globals()["tag_%s" % tag.tag.replace("-", "_")]
    return join(fun(tag, parent))

def parse_subtags(tag, parent=None):
    yield tag.text

    for subtag in tag:
        yield parse_tag(subtag, tag)

    yield tag.tail

def handle_subject(tag, parent):
    yield '?%s\n' % tag.attrib['id']

    first = True
    for element in tag:
        if element.tag in ["para", "variablelist", "simplelist",
                           "command-list", "ircexample"]:
            if not first:
                yield "\n"
            first = False

            if element.attrib.get('title', ''):
                yield element.attrib['title']
                yield "\n"
            yield join(parse_tag(element, tag)).rstrip("\n")
            yield "\n"

    yield "%\n"

    for element in tag:
        if element.tag in ["sect1", "sect2"]:
            yield join(handle_subject(element, tag))

    for element in tag.findall("bitlbee-command"):
        yield join(handle_command(element))

    for element in tag.findall("bitlbee-setting"):
        yield join(handle_setting(element))

def handle_command(tag, prefix=''):
    this_cmd = prefix + tag.attrib['name']

    yield "?%s\n" % this_cmd
    for syntax in tag.findall("syntax"):
        yield '\x02Syntax:\x02 %s\n' % syntax.text

    yield "\n"
    yield join(parse_subtags(tag.find("description"))).rstrip()
    yield "\n"

    for example in tag.findall("ircexample"):
        yield "\n\x02Example:\x02\n"
        yield join(parse_subtags(example)).rstrip()
        yield "\n"

    yield "%\n"

    for element in tag.findall("bitlbee-command"):
        yield join(handle_command(element, this_cmd + " "))

def handle_setting(tag):
    yield "?set %s\n" % tag.attrib['name']
    yield "\x02Type:\x02 %s\n" % tag.attrib["type"]
    yield "\x02Scope:\x02 %s\n" % tag.attrib["scope"]

    if tag.find("default") is not None:
        yield "\x02Default:\x02 %s\n" % tag.findtext("default")

    if tag.find("possible-values") is not None:
        yield "\x02Possible Values:\x02 %s\n" % tag.findtext("possible-values")

    yield "\n"
    yield join(parse_subtags(tag.find("description"))).rstrip()
    yield "\n%\n"

tag_preface = handle_subject
tag_chapter = handle_subject
tag_sect1 = handle_subject
tag_sect2 = handle_subject

tag_ulink = parse_subtags
tag_note = parse_subtags
tag_book = parse_subtags
tag_ircexample = parse_subtags

def tag_include(tag, parent):
    return parse_file(tag.attrib['href'], tag)

def tag_para(tag, parent):
    return join(parse_subtags(tag)) + "\n\n"

def tag_emphasis(tag, parent):
    return "\x02%s\x02%s" % (tag.text, tag.tail)

def tag_ircline(tag, parent):
    return "\x02<%s>\x02 %s\n" % (tag.attrib['nick'], join(parse_subtags(tag)))

def tag_ircaction(tag, parent):
    return "\x02* %s\x02 %s\n" % (tag.attrib['nick'], join(parse_subtags(tag)))

def tag_command_list(tag, parent):
    yield "These are all root commands. See \x02help <command name>\x02 " \
          "for more details on each command.\n\n"

    for subtag in parent.findall("bitlbee-command"):
        yield " * \x02%s\x02 - %s\n" % \
            (subtag.attrib['name'],
             subtag.findtext("short-description"))

    yield "\nMost commands can be shortened. For example instead of " \
          "\x02account list\x02, try \x02ac l\x02.\n\n"

def tag_variablelist(tag, parent):
    for subtag in tag:
        yield " \x02%s\x02 - %s\n" % \
            (subtag.findtext("term"),
             join(parse_subtags(subtag.find("listitem/para"))))
    yield '\n'

def tag_simplelist(tag, parent):
    for subtag in tag:
        yield " - %s\n" % join(parse_subtags(subtag))
    yield '\n'

def main():
    txt = parse_file(IN_FILE)
    open(OUT_FILE, "w").write(txt)

if __name__ == '__main__':
    main()
