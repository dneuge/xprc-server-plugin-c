from hashlib import md5
import sys
import os.path
tool_dir = os.path.dirname(__file__)+os.sep+os.pardir
sys.path.append(tool_dir + os.sep + 'simple-sbom' + os.sep + 'python')

from argparse import ArgumentParser

from simple_sbom import *

arg_parser = ArgumentParser()
arg_parser.add_argument('--activation', type=str, nargs='+', default=[], metavar='TAG', help='active dependency tags')

args = arg_parser.parse_args()


UTF_8_FILE_ENCODING = 'utf-8'
ASCII_FILE_ENCODING = 'ascii'

root_dir = tool_dir + os.sep + os.pardir
src_dir = root_dir + os.sep + 'src'

licenses_dir = root_dir + os.sep + 'licenses'
spdx_licenses_dir = licenses_dir + os.sep + 'spdx'

license_out_path = src_dir + os.sep + '_licenses.c'
dependencies_out_path = src_dir + os.sep + '_dependencies.c'
trademarks_out_path = src_dir + os.sep + '_trademarks.c'

generated_source_note = '// THIS FILE HAS BEEN AUTOMATICALLY GENERATED AND WILL BE OVERWRITTEN DURING BUILD, DO NOT EDIT'

def load_text_file(path:str) -> str:
    if not os.path.exists(path):
        raise ValueError(f'File does not exist: {path}')

    with open(path, 'r', encoding=UTF_8_FILE_ENCODING) as fh:
        return fh.read()

def load_spdx_license(spdx_id:str) -> str:
    return load_text_file(spdx_licenses_dir + os.sep + spdx_id + '.txt')

def is_7bit_ascii(s:str) -> bool:
    for ch in s:
        v = ord(ch)
        if v > 127:
            return False
    return True

# protect output from being accidentally overwritten
def require_new_or_generated(path:str):
    if os.path.exists(path):
        previous_license_out = load_text_file(path).splitlines()
        if previous_license_out[0] != generated_source_note:
            raise ValueError(f'output file already exists but does not hold expected header; check if correct and delete: {path}')

require_new_or_generated(license_out_path)
require_new_or_generated(dependencies_out_path)
require_new_or_generated(trademarks_out_path)

license_names : dict[str,str] = {}
license_texts : dict[str,str] = {}

# import all licenses declared in SBOM
sbom_path = root_dir + os.sep + 'sbom.xml'
print(f'Loading {sbom_path}')
sbom = SimpleSBOM.parse_file(sbom_path)
for license in sbom.licenses.values():
    text : str|None = license.text
    if text is None:
        if license.standard is None:
            raise ValueError(f'SBOM-defined license "{license.id}" has no text but is also non-standard')

        if license.standard.spdx is None:
            raise ValueError(f'SBOM-defined license "{license.id}" has no text but also has no SPDX identifier')

        if len(license.standard.variations) > 0:
            raise ValueError(f'SBOM-defined license "{license.id}" has variations deviating from standard SPDX text, unable to apply generic license')

        text = load_spdx_license(license.standard.spdx)

    license_names[license.id] = license.id  # FIXME: define in SBOM or include for all SPDX licenses?
    license_texts[license.id] = text

print('Formatting licenses...')

# establish alphabetic license order prior to prepending our binary license
license_ids = list(license_texts.keys())
license_ids.sort()

# add our own distribution license, prepended
XPRC_BINARY_LICENSE_ID = '_xprc-binary'
license_names[XPRC_BINARY_LICENSE_ID] = 'XPRC binary license'
license_texts[XPRC_BINARY_LICENSE_ID] = load_text_file(licenses_dir + os.sep + 'xprc-binary-distribution.txt')
license_ids : list[str] = [XPRC_BINARY_LICENSE_ID] + license_ids

license_out : list[str] = [
    generated_source_note,
    '',
    '// This file holds all license texts relevant for binary distribution of XPRC.',
    '// All licenses remain under their original owners\' copyright and licenses,',
    '// the whole purpose of collecting them here is to reproduce them in the compiled',
    '// binary to comply with the terms generally set by those licenses.',
    '',
    '#include <stdio.h>',
    '',
    '#include "licenses.h"',
    '',
]

def format_output_index(index:int):
    return '%04d' % index

def format_c_string_content(s:str) -> str:
    s = s.replace('\u00ae', '(R)')

    if not is_7bit_ascii(s):
        raise ValueError('unable to encode; not 7-bit ASCII: '+s)
    return s.replace('\\', '\\\\').replace('"', '\\"')

def format_c_multiline_string(s:str, indention:str='    ', rstrip:bool = False) -> list[str]:
    if rstrip:
        s = s.rstrip()

    out : list[str] = []
    lines = s.splitlines()
    for i in range(len(lines)):
        line : str = lines[i]
        is_last_line = (i == len(lines)-1)

        if rstrip:
            line = line.rstrip()

        line_out : str = indention
        line_out += '"'
        line_out += format_c_string_content(line)
        if not is_last_line or not rstrip:
            line_out += "\\n"
        line_out += '"'

        out.append(line_out)

    return out

MAX_UINT32 = (1 << 32) - 1
def hash_to_uint32(s:str)->int:
    # reduce to MD5 to 32 bits by XOR'ing all uint32 read from 4 bytes each
    out = 0
    bs = md5(s.encode(ASCII_FILE_ENCODING)).digest()
    for i in range(0,4):
        v = 0
        for j in range(0,4):
            b = bs[i*4 + j]
            v = v * 256 + b
        out = out ^ v
    return out

output_index : int = 0
license_out_links : list[str] = []
for license_id in license_ids:
    text = license_texts[license_id]
    if not is_7bit_ascii(text):
        raise ValueError(f'license text for {license_id} contains extended characters; all texts must be 7-bit ASCII')

    output_index_string = format_output_index(output_index)

    license_out.append('static const char _xprc_license__id_%s[] = "%s";' % (output_index_string, format_c_string_content(license_id)))
    license_out.append('static const char _xprc_license__name_%s[] = "%s";' % (output_index_string, format_c_string_content(license_names[license_id])))
    license_out.append('static const uint32_t _xprc_license__hash_%s = %d;' % (output_index_string, hash_to_uint32(text)))
    license_out.append('static const char _xprc_license__text_%s[] =' % output_index_string)
    license_out += format_c_multiline_string(text)
    license_out.append(';')
    license_out.append('')

    license_out_links.append('    {')
    license_out_links.append('        .id   = (char*) _xprc_license__id_%s,' % output_index_string)
    license_out_links.append('        .name = (char*) _xprc_license__name_%s,' % output_index_string)
    license_out_links.append('        .text = (char*) _xprc_license__text_%s,' % output_index_string)
    license_out_links.append('        .hash = _xprc_license__hash_%s,' % output_index_string)
    license_out_links.append('    },')

    output_index += 1

license_out.append('static const xprc_license_t _xprc_licenses[] = {')
license_out += license_out_links
license_out.append('    {')
license_out.append('        .id   = NULL,')
license_out.append('        .name = NULL,')
license_out.append('        .text = NULL,')
license_out.append('        .hash = 0,')
license_out.append('    }')
license_out.append('};')

print(f'Writing {license_out_path}')
with open(license_out_path, 'w', encoding=ASCII_FILE_ENCODING) as fh:
    fh.write('\n'.join(license_out))
print('... done')



print('Formatting dependencies...')

dependencies_out : list[str] = [
    generated_source_note,
    '',
    '#include <stdio.h>',
    '',
    '#include "dependencies.h"',
    '',
]

active_tags : list[str] = args.activation
print('Active tags:', active_tags)
tag_descriptions : dict[str, str] = {
    # all tags used in SBOM must be listed here
    # will be concatenated to "only used if ... [or ...]"
    'compiler-mingw': 'compiled with MinGW',
    'target-macos': 'compiled for macOS',
}

def enumerate_sentence(conjunction:str, items:Iterable[str]) -> str:
    items = list(items)
    if len(items) == 0:
        raise ValueError('at least one item is required')
    elif len(items) == 1:
        return items[0]
    else:
        return ', '.join(items[:-1]) + ' ' + conjunction + ' ' + items[-1]

def format_years(years:Iterable[CopyrightYear]) -> str:
    out = ''

    for year in years:
        if out != '':
            out += ', '

        if not isinstance(year, tuple):
            out += str(year)
        else:
            out += f'{year[0]}-{year[1]}'

    return out

def format_legal_successors(author:LegalEntity) -> str:
    out : str = ''

    for info in author.successors:
        successor : LegalEntity = info.entity

        out += f'{author.name} has been succeeded by {successor.name}'
        if info.date is not None:
            out += f' in {info.date.year}'
        out += '\n'

        out += format_legal_successors(successor)

    return out.rstrip()

def format_copyright(item:Copyright) -> str:
    out : str = ''

    # Prefer the copyright information that is most accurate for reproduction:
    #   1. original remarks (unmodified)
    #   2. combined author name representations (usually unmodified)
    #   3. individual author names (as per LegalEntity)
    if len(item.original_remarks) > 0:
        for remark in item.original_remarks:
            # add an empty line to space multi-line text blocks apart from any previous collated one-liners or other blocks
            has_multiple_lines = '\n' in remark
            if has_multiple_lines and len(out) > 0:
                out += '\n'
            out += remark.rstrip() + '\n\n'
    else:
        author_names: str = ''
        if item.combined_authors is not None and len(item.combined_authors) > 0:
            author_names = item.combined_authors
        else:
            author_names = enumerate_sentence('and', [author.name for author in item.authors])

        out += 'Copyright (c) '
        if len(item.years) > 0:
            out += format_years(item.years) + ' '
        out += author_names

    # TODO: collect and list successors separately (avoid duplicates, show at end of overall copyright notice)
    for author in item.authors:
        successor_out: str = format_legal_successors(author)
        if successor_out != '':
            out += '\n\n'
            out += successor_out

    return out

def prefix_all_lines(prefix:str, s:str) -> str:
    out : str = ''

    for line in s.splitlines():
        out += prefix + line + '\n'

    return out

output_index = 0
dependencies_out_links : list[str] = []
for dependency in sorted(sbom.dependencies.values(), key=lambda x: x.name.lower()):
    if len(dependency.copyrights) == 0:
        print(f'Dependency has no copyrights, skipping: {dependency.id}')
        continue

    if dependency.method == DependencyMethod.PROVIDED:
        print(f'Dependency is provided, skipping: {dependency.id}')
        continue

    output_index_string = format_output_index(output_index)

    out_activation : str = ''
    has_activation : bool = len(dependency.activation_tags) > 0
    if has_activation:
        for tag in dependency.activation_tags:
            if out_activation != '':
                out_activation += ' or '
            out_activation += tag_descriptions[tag].strip()
        out_activation = 'only used if ' + out_activation

    is_active_raw: bool|None = dependency.is_active(active_tags)
    is_active : bool = is_active_raw if is_active_raw is not None else True

    out_url : str|None = None
    if len(dependency.websites) > 0:
        out_url = dependency.websites[0]

    dependencies_out.append('static const char _xprc_dependency_%s_id[] = "%s";' % (output_index_string, format_c_string_content(dependency.id)))
    dependencies_out.append('static const char _xprc_dependency_%s_name[] = "%s";' % (output_index_string, format_c_string_content(dependency.name)))
    if dependency.version is not None:
        dependencies_out.append('static const char _xprc_dependency_%s_version[] = "%s";' % (output_index_string, format_c_string_content(dependency.version)))
    else:
        dependencies_out.append('// _xprc_dependency_%s has no version' % output_index_string)
    if out_url is not None:
        dependencies_out.append('static const char _xprc_dependency_%s_url[] = "%s";' % (output_index_string, format_c_string_content(out_url)))
    else:
        dependencies_out.append('// _xprc_dependency_%s has no url' % output_index_string)
    if dependency.excerpt is not None:
        dependencies_out.append('static const char _xprc_dependency_%s_excerpt[] = "%s";' % (output_index_string, format_c_string_content(dependency.excerpt)))
    else:
        dependencies_out.append('// _xprc_dependency_%s has no excerpt' % output_index_string)
    dependencies_out.append('static const bool _xprc_dependency_%s_active = %s;' % (output_index_string, 'true' if is_active else 'false'))
    if has_activation:
        dependencies_out.append('static const char _xprc_dependency_%s_activation[] = "%s";' % (output_index_string, format_c_string_content(out_activation)))
    else:
        dependencies_out.append('// _xprc_dependency_%s has no activation conditions' % output_index_string)
    dependencies_out.append('')

    # collect all copyrights before formatting output
    # tuple is license ID + copyright text (multi-line)
    copyrights : list[tuple[str|None,str]] = []
    for item in dependency.copyrights:
        license_id : str|None = item.license.id if item.license is not None else None
        text : str = format_copyright(item)
        copyrights.append((license_id, text))

    if len(dependency.patches) > 0:
        text : str = 'With additional patches:\n'

        for patch in dependency.patches:
            text += ' - ' + patch.description + '\n'
            for item in patch.copyrights:
                if item.license is not None:
                    text += '   made available under '+item.license.name+' license\n'
                text += prefix_all_lines('   ', format_copyright(item))

        copyrights.append((None, text))

    dependencies_out_copyright_links: list[str] = []
    copyright_index = 0
    for license_id, text in copyrights:
        copyright_index_string = format_output_index(copyright_index)

        if license_id is not None:
            dependencies_out.append('static const char _xprc_dependency_copyright_%s_%s_license_id[] = "%s";' % (output_index_string, copyright_index_string, format_c_string_content(license_id)))
        else:
            dependencies_out.append('// _xprc_dependency_copyright_%s_%s has no license_id' % (output_index_string, copyright_index_string))
        dependencies_out.append('static const char _xprc_dependency_copyright_%s_%s_copyright_remark[] =' % (output_index_string, copyright_index_string))
        dependencies_out += format_c_multiline_string(text, rstrip=True)
        dependencies_out.append(';')
        dependencies_out.append('')

        dependencies_out_copyright_links.append('    {')
        if license_id is not None:
            dependencies_out_copyright_links.append('        .license_id = (char*) _xprc_dependency_copyright_%s_%s_license_id,' % (output_index_string, copyright_index_string))
        else:
            dependencies_out_copyright_links.append('        .license_id = NULL,')
        dependencies_out_copyright_links.append('        .copyright_remark = (char*) _xprc_dependency_copyright_%s_%s_copyright_remark,' % (output_index_string, copyright_index_string))
        dependencies_out_copyright_links.append('    },')

        copyright_index += 1

    dependencies_out.append('static const xprc_dependency_copyright_t _xprc_dependency_copyright_%s[] = {' % output_index_string)
    dependencies_out += dependencies_out_copyright_links
    dependencies_out.append('    {')
    dependencies_out.append('        .license_id = NULL,')
    dependencies_out.append('        .copyright_remark = NULL,')
    dependencies_out.append('    }')
    dependencies_out.append('};')
    dependencies_out.append('')

    dependencies_out_links.append('    {')
    dependencies_out_links.append('        .id = (char*) _xprc_dependency_%s_id,' % output_index_string)
    dependencies_out_links.append('        .name = (char*) _xprc_dependency_%s_name,' % output_index_string)
    if dependency.version is not None:
        dependencies_out_links.append('        .version = (char*) _xprc_dependency_%s_version,' % output_index_string)
    else:
        dependencies_out_links.append('        .version = NULL,')
    if out_url is not None:
        dependencies_out_links.append('        .url = (char*) _xprc_dependency_%s_url,' % output_index_string)
    else:
        dependencies_out_links.append('        .url = NULL,')
    if dependency.excerpt is not None:
        dependencies_out_links.append('        .excerpt = (char*) _xprc_dependency_%s_excerpt,' % output_index_string)
    else:
        dependencies_out_links.append('        .excerpt = NULL,')
    dependencies_out_links.append('        .active = _xprc_dependency_%s_active,' % output_index_string)
    if has_activation:
        dependencies_out_links.append('        .activation = (char*) _xprc_dependency_%s_activation,' % output_index_string)
    else:
        dependencies_out_links.append('        .activation = NULL,')
    dependencies_out_links.append('        ._copyrights = (xprc_dependency_copyright_t*) _xprc_dependency_copyright_%s,' % output_index_string)
    dependencies_out_links.append('    },')

    output_index += 1

dependencies_out.append('static const xprc_dependency_t _xprc_dependencies[] = {')
dependencies_out += dependencies_out_links
dependencies_out.append('    {')
dependencies_out.append('        .id = NULL,')
dependencies_out.append('        .name = NULL,')
dependencies_out.append('        .version = NULL,')
dependencies_out.append('        .url = NULL,')
dependencies_out.append('        .excerpt = NULL,')
dependencies_out.append('        .active = true,')
dependencies_out.append('        .activation = NULL,')
dependencies_out.append('        ._copyrights = NULL,')
dependencies_out.append('    },')
dependencies_out.append('};')

print(f'Writing {dependencies_out_path}')
with open(dependencies_out_path, 'w', encoding=ASCII_FILE_ENCODING) as fh:
    fh.write('\n'.join(dependencies_out))
print('... done')


trademarks_out : list[str] = [
    generated_source_note,
    '',
    '// This file holds all trademark information and other acknowledgements relevant',
    '// for binary distribution of XPRC.',
    '// The whole purpose of collecting them here is to reproduce them in the compiled',
    '// binary to comply with the trademark owner\'s trademark policies etc.',
    '// Doing so does NOT indicate or imply any kind of affiliation with any of the',
    '// parties listed here.',
    '',
    '#include <stdio.h>',
    '',
]

output_index = 0
trademarks_out_links: list[str] = []
for trademark in sorted(sbom.trademarks, key=lambda x: x.display.strip().lower()):
    output_index_string = format_output_index(output_index)

    trademarks_out.append('static const char _xprc_trademark_acknowledgment_%s[] =' % output_index_string)
    trademarks_out += format_c_multiline_string(trademark.display, rstrip=True)
    trademarks_out.append(';')
    trademarks_out.append('')

    trademarks_out_links.append('    _xprc_trademark_acknowledgment_%s,' % output_index_string)

    output_index += 1

trademarks_out.append('static const char *_xprc_trademarks_acknowledgments[] = {')
trademarks_out += trademarks_out_links
trademarks_out.append('    NULL,')
trademarks_out.append('};')

print(f'Writing {trademarks_out_path}')
with open(trademarks_out_path, 'w', encoding=ASCII_FILE_ENCODING) as fh:
    fh.write('\n'.join(trademarks_out))
print('... done')
