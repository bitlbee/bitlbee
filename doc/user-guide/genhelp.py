#!/usr/bin/env python

# Usage: python genhelp.py input.xml output.txt
# (Both python2 (>=2.5) or python3 work)
#
# The shebang above isn't used, set the PYTHON environment variable
# before running ./configure instead

# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.


import os
import re
import sys
import xml.etree.ElementTree as ET

NORMALIZE_RE = re.compile(r"([^<>\s\t])[\s\t]+([^<>\s\t])")

# Helpers

def normalize(x):
    """Normalize whitespace of a string.

    The regexp turns any sequence of whitespace into a single space if it's in
    the middle of the tag text, and then all newlines and tabs are removed,
    keeping spaces.
    """

    x = NORMALIZE_RE.sub(r"\1 \2", x or '')
    return x.replace("\n", "").replace("\t", "")

def join(list):
    """Turns any iterator into a string"""
    return ''.join([str(x) for x in list])

def fix_tree(tag, debug=False, lvl=''):
    """Walks the XML tree and modifies it in-place fixing various details"""

    # The include tags have an ugly namespace in the tag name. Simplify that.
    if tag.tag.count("XInclude"):
        tag.tag = 'include'

    # Print a pretty tree-like representation of the processed tags
    if debug:
        print("%s<%s>%r" % (lvl, tag.tag, [tag.text, normalize(tag.text)]))

    for subtag in tag:
        fix_tree(subtag, debug, lvl + "  ")

    if debug:
        print("%s</%s>%r" % (lvl, tag.tag, [tag.tail, normalize(tag.tail)]))

    # Actually normalize whitespace
    if 'pre' not in tag.attrib:
        tag.text = normalize(tag.text)
    tag.tail = normalize(tag.tail)


# Main logic

def process_file(filename, parent=None):
    try:
        tree = ET.parse(open(filename)).getroot()
    except:
        sys.stderr.write("\nException while processing %s\n" % filename)
        raise
    fix_tree(tree)
    return parse_tag(tree, parent)

def parse_tag(tag, parent):
    """Calls a tag_... function based on the tag name"""

    fun = globals()["tag_%s" % tag.tag.replace("-", "_")]
    return join(fun(tag, parent))

def parse_subtags(tag, parent=None):
    yield tag.text

    for subtag in tag:
        yield parse_tag(subtag, tag)

    yield tag.tail


# Main tag handlers

def handle_subject(tag, parent):
    """Tag handler for preface, chapter, sect1 and sect2 (aliased below)"""

    yield '?%s\n' % tag.attrib['id']

    first = True
    for element in tag:
        if element.tag in ["para", "variablelist", "simplelist",
                           "command-list", "ircexample"]:
            if not first:
                # Spaces between paragraphs
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
    """Tag handler for <bitlbee-command> (called from handle_subject)"""

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
    """Tag handler for <bitlbee-setting> (called from handle_subject)"""

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


# Aliases for tags that behave like subjects
tag_preface = handle_subject
tag_chapter = handle_subject
tag_sect1 = handle_subject
tag_sect2 = handle_subject

# Aliases for tags that don't have any special behavior
tag_ulink = parse_subtags
tag_note = parse_subtags
tag_book = parse_subtags
tag_ircexample = parse_subtags


# Handlers for specific tags

def tag_include(tag, parent):
    return process_file(tag.attrib['href'], tag)

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
    if len(sys.argv) != 3:
        print("Usage: python genhelp.py input.xml output.txt")
        return

    # ensure that we really are in the same directory as the input file
    os.chdir(os.path.dirname(os.path.abspath(sys.argv[1])))

    txt = process_file(sys.argv[1])
    open(sys.argv[2], "w").write(txt)

if __name__ == '__main__':
    main()
