from __future__ import division, with_statement
from optparse import OptionParser
import re
import pprint
import xml.dom.minidom
import os
import logging
import logging.handlers
import sys

from writer import HTMLWriter, TextWriter, XMLWriter
from UserDict import DictMixin

VERSION = '1.6'

class odict(DictMixin):
    def __init__(self):
        self._keys = []
        self._data = {}
        
    def __setitem__(self, key, value):
        if key not in self._data:
            self._keys.append(key)
        self._data[key] = value
        
    def __getitem__(self, key):
        return self._data[key]
    
    def __delitem__(self, key):
        del self._data[key]
        self._keys.remove(key)
        
    def keys(self):
        return list(self._keys)
    
    def copy(self):
        copyDict = odict()
        copyDict._data = self._data.copy()
        copyDict._keys = self._keys[:]
        return copyDict


class Entry(object):
    def __init__(self, raw_data, ugly):
        self.raw_data = raw_data
        m = re.match(r'(?P<year>\d{2})(?P<month>\d{2})(?P<day>\d{2}) (?P<time>\d{2}:\d{2}:\d{2}) +(?P<entry>\d+) AccountServer\[1\] ESC : Transaction (?P<trans_id>\d+) (?P<type>(request|response)): (?P<data>.+)', raw_data)
        if not m:
            raise RuntimeError('Entry has invalid syntax')

        self.year = '20' + m.group('year')
        self.month = m.group('month')
        self.day = m.group('day')
        self.date = str(self.year) + ':' + str(self.month) + ':' + str(self.day)
        self.time = m.group('time')
        self.log_entry = m.group('entry')
        self.trans_id = m.group('trans_id')
        self.type = m.group('type')
        self.xml = m.group('data')
        self.id = m.group('entry')

        if self.type == 'request':
            s = re.search(r'<soapenv:Body>\\n<(?P<action>[\w:]+)', self.xml)
            if s:
                self.action = s.group('action')
            else:
                self.action = ''
            self.code = ''
        else:
            s = re.search(r'<SOAP-ENV:Body\\n  >\\n    <namesp\d+:(?P<action>[\w:]+) xmlns:namesp\d+=\\qhttp://soap.vindicia.com/(?P<object>\w+)', self.xml)
            if s:
                self.action = s.group('object') + ':' + s.group('action')
            else:
                self.action = ''
            s = re.search(r'(?P<code>\d+)</returnCode>', self.xml)
            if s:
                self.code = s.group('code')
            else:
                self.code = ''

        # Cleanup XML
        self.xml = re.sub(r'\\q', '"', self.xml)
        self.xml = re.sub(r'\\n', "\n", self.xml)
        self.xml = re.sub(r'<password X+</password>', '<password>(redacted)</password>', self.xml)
        self.xml = re.sub(r'<account X+</account>', '<account>(redacted)</account>', self.xml)
        self.xml = re.sub(r'<item xsi:type="vin:NameValuePair"><X+/nameValues>', '<item xsi:type="vin:NameValuePair">(redacted)</item></nameValues>', self.xml)

        if ugly:
            self.xml = self.xml + '\n'
            self.is_valid_xml = False
        else:
            try:
                dom = xml.dom.minidom.parseString(self.xml)
                self.xml = dom.toprettyxml()
                self.is_valid_xml = True
            except xml.parsers.expat.ExpatError:
                self.xml = self.xml + '\n'
                self.is_valid_xml = False

        self.xml = re.sub(r'\n *\n', '\n', self.xml)
        self.xml = re.sub(r'\n\t*\n', '\n', self.xml)


def clean(logger, ugly, format, input_file, output_file, only_trans_id=None, xml_dir=None):
    logger.debug('Starting...')

    entries = list()
    cur_entry = 1

    logger.debug('Reading input...')

    for raw_data in input_file:
        try:
            try:
                entry = Entry(raw_data.decode('utf-8'), ugly)
            except UnicodeEncodeError:
                entry = Entry(raw_data, ugly)
            
            if entry.is_valid_xml:
                validity = '(valid XML)'
            else:
                validity = '(invalid XML)'

            if not only_trans_id or only_trans_id == entry.trans_id:
                logger.debug('Adding entry ' + str(cur_entry) + ': ' +
                        entry.date + ' ' + entry.time + ' ' +
                        entry.trans_id + ' ' + entry.type + ' ' +
                        validity)
                entries.append(entry)
                cur_entry = cur_entry + 1

        except RuntimeError:
            pass

    logger.debug('Sorting by trans_id...')

    entry_groups = odict()
    num_groups = 0

    for entry in entries:
        trans_id = entry.trans_id
        if not trans_id in entry_groups:
            entry_groups[trans_id] = list()
            num_groups = num_groups + 1

        entry_groups[trans_id].append(entry)

    logger.debug('Writing report...')

    if xml_dir:
        writer = XMLWriter(xml_dir)
    elif format == 'text':
        writer = TextWriter(output_file)
    elif format == 'html':
        writer = HTMLWriter(output_file)
    else:
        raise RuntimeError("Unsupported output format.")

    writer.write_top()
    writer.write_header(num_groups, cur_entry - 1)
    writer.write_summary(entry_groups)
    writer.write_details(entry_groups)
    writer.write_bottom()

    logger.debug('Stopping...')
    logger.info(str(cur_entry - 1) + ' entries parsed')
    logger.info(str(num_groups) + ' transaction chains')


if __name__ == '__main__':
    parser = OptionParser(usage='usage: %prog [options]', version='%prog ' + VERSION)
    parser.add_option('-v', '--verbose',
            action='store_true', dest='verbose', help='log debug information');
    parser.add_option('-t', '--trans', metavar='ID', dest='trans', type='string', help='only include the given trans ID')
    parser.add_option('-x', '--xml', metavar='DIR', dest='dir', type='string', help='write XML files in DIR')
    parser.add_option('-u', '--ugly',
            action='store_true', dest='ugly', help='do not pretty-print XML');
    parser.add_option('-o', '--output', metavar='FILE', dest='output', type='string', default=sys.stdout, help='output file for report (defaults to STDOUT)')
    parser.add_option('-i', '--input', metavar='FILE', dest='input', type='string', default=sys.stdin, help='input file for report (defaults to STDIN)')
    parser.add_option('-a', '--autoopen', action='store_true', dest='autoopen', help='automatically open output file in default editor (requires -o option be specified)')
    parser.add_option('-f', '--format', dest='format', type='string', default='html', help='report format (text, html) (defaults to html)')
    
    (options, args) = parser.parse_args()
    if len(args) != 0:
        parser.error('incorrect number of arguments')

    logger = logging.getLogger('CleanLogger')

    handler = logging.handlers.RotatingFileHandler('clean.log')

    if options.verbose:
        logger.setLevel(logging.DEBUG)
    else:
        logger.setLevel(logging.INFO)

    formatter = logging.Formatter('%(asctime)s - %(levelname)s - %(message)s')
    handler.setFormatter(formatter)

    logger.addHandler(handler)

    logger.info('Starting version ' + VERSION)

    if options.input != sys.stdin:
        options.input = open(options.input)

    if options.output != sys.stdout:
        out_filename = options.output
        options.output = open(out_filename, 'w')

    clean(logger, options.ugly, options.format, options.input, options.output, options.trans, options.dir)

    options.output.flush()

    if options.autoopen and options.output != sys.stdout:
        os.startfile(out_filename)
