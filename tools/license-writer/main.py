from hashlib import md5
import sys
import os.path
tool_dir = os.path.dirname(__file__)+os.sep+os.pardir
sys.path.append(tool_dir + os.sep + 'simple-sbom' + os.sep + 'python')

from simple_sbom import *

UTF_8_FILE_ENCODING = 'utf-8'
ASCII_FILE_ENCODING = 'ascii'

root_dir = tool_dir + os.sep + os.pardir
src_dir = root_dir + os.sep + 'src'

licenses_dir = root_dir + os.sep + 'licenses'
spdx_licenses_dir = licenses_dir + os.sep + 'spdx'

license_out_path = src_dir + os.sep + '_licenses.c'

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
if os.path.exists(license_out_path):
    previous_license_out = load_text_file(license_out_path).splitlines()
    if previous_license_out[0] != generated_source_note:
        raise ValueError(f'output file already exists but does not hold expected header; check if correct and delete: {license_out_path}')

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

license_out = [
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
    return s.replace('\\', '\\\\').replace('"', '\\"')

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
    for line in text.splitlines():
        license_out.append('    "' + format_c_string_content(line) + '\\n"')
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