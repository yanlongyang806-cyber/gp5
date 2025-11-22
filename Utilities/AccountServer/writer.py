from __future__ import division, with_statement
import os

def html_escape(text):
    """Produce entities within text."""

    html_escape_table = {
        "&": "&amp;",
        '"': "&quot;",
        "'": "&apos;",
        ">": "&gt;",
        "<": "&lt;",
    }
    return "".join(html_escape_table.get(c,c) for c in text)


class HTMLWriter(object):
    """ Write the Vindicia log entries to an HTML file """

    def __init__(self, output_file):
        self.output_file = output_file

    def write_top(self):
        self.output_file.write('<html><head><title>Vindicia Report</title>')

        self.output_file.write('<script src="http://code.jquery.com/jquery-1.4.2.min.js" type="text/javascript"></script>')
        self._write_css_file('clean/clean.css')
        self._write_css_file('clean/SyntaxHighlighter.css')
        self._write_js_file('clean/shCore.js')
        self._write_js_file('clean/shBrushXml.js')
        self._write_js_file('clean/boc.js')
        self._write_js_file('clean/clean.js')
        self.output_file.write('</head><body>')

    def write_header(self, num_chains, num_entries):
        self.output_file.write('Transaction chains: ' + str(num_chains) + '\n')
        self.output_file.write('Total entries: ' + str(num_entries) + '\n')

    def write_summary(self, entry_groups):
        pass

    def write_details(self, entry_groups):
        for trans_id in entry_groups.keys():
            entry_list = entry_groups[trans_id]

            self.output_file.write('<h2>Trans ID ' + trans_id + '</h2>')

            for entry in entry_list:
                self.output_file.write('<h3><a href="#">' + entry.date + ' ' + entry.time + ': ' + entry.type + ' ' + entry.action)
                if len(entry.code) > 0:
                    self.output_file.write(' (' + entry.code + ')')
                self.output_file.write('</a></h3>')

                try:
                    self._write_xml(entry.xml.encode('utf-8'))
                except UnicodeDecodeError:
                    self._write_xml(entry.xml)
    
    def write_bottom(self):
        self.output_file.write('</body></html>')

    def _write_js_file(self, js_file):
        self.output_file.write('<script type="text/javascript">')
        self.output_file.write(open(js_file, 'r').read())
        self.output_file.write('</script>')

    def _write_css_file(self, css_file):
        self.output_file.write('<style type="text/css">')
        self.output_file.write(open(css_file, 'r').read())
        self.output_file.write('</style>')

    def _write_trans_summary(self, entry_list):
        for entry in entry_list:
            self.output_file.write(entry.date + ' ' + entry.time + ' ' + entry.type)
            if len(entry.action) > 0:
                self.output_file.write(' ' + entry.action)
            self.output_file.write(' ' + entry.code + '\n')

    def _write_xml(self, xml):
        self.output_file.write('<pre><code>')
        self.output_file.write(html_escape(xml))
        self.output_file.write('</code></pre>')


class TextWriter(object):
    """ Write the Vindicia log entries to a text file """

    def __init__(self, output_file):
        self.output_file = output_file

    def write_top(self):
        pass

    def write_header(self, num_chains, num_entries):
        self.output_file.write('Transaction chains: ' + str(num_chains) + '\n')
        self.output_file.write('Total entries: ' + str(num_entries) + '\n')

    def write_summary(self, entry_groups):
        for trans_id in entry_groups.keys():
            entry_list = entry_groups[trans_id]
            self.output_file.write('\n----<? Trans ID: ' + trans_id + ' ?>----\n')
            self.output_file.write('Entries: ' + str(len(entry_list)) + '\n')
            self.output_file.write('\n')
            self._write_trans_summary(entry_list)
            self.output_file.write('\n')

    def write_details(self, entry_groups):
        for trans_id in entry_groups.keys():
            entry_list = entry_groups[trans_id]

            self.output_file.write('\n\n---------------------------------------------------\n')
            self.output_file.write('Trans ID: ' + trans_id + '\n')
            self.output_file.write('Entries: ' + str(len(entry_list)) + '\n')
            self.output_file.write('\nSummary:\n')
            self._write_trans_summary(entry_list)
            self.output_file.write('\nEntries:\n\n')

            for entry in entry_list:
                self.output_file.write(entry.action + '\n')
                self.output_file.write('Timestamp: ' + entry.date + ' ' + entry.time + '\n')
                self.output_file.write('Type: ' + entry.type + '\n')
                if len(entry.code) > 0:
                    self.output_file.write('Return code: ' + entry.code + '\n')

                try:
                    self._write_xml(entry.xml.encode('utf-8'))
                except UnicodeDecodeError:
                    self._write_xml(entry.xml)

    def write_bottom(self):
        pass

    def _write_trans_summary(self, entry_list):
        for entry in entry_list:
            self.output_file.write(entry.date + ' ' + entry.time + ' ' + entry.type)
            if len(entry.action) > 0:
                self.output_file.write(' ' + entry.action)
            self.output_file.write(' ' + entry.code + '\n')

    def _write_xml(self, xml):
        self.output_file.write(xml + '\n')


class XMLWriter(object):
    """ Write to a series of XML files in a directory """

    def __init__(self, xml_dir):
        self.xml_dir = xml_dir

    def write_top(self):
        pass

    def write_header(self, num_chains, num_entries):
        pass

    def write_summary(self, entry_groups):
        pass

    def write_details(self, entry_groups):
        for trans_id in entry_groups.keys():
            entry_list = entry_groups[trans_id]

            cur_dir = self.xml_dir + '/' + trans_id
            os.makedirs(cur_dir)

            for entry in entry_list:
                cur_file = entry.id + '_' + entry.action.replace(':', '_') + '.xml'
                with open(cur_dir + '/' + cur_file, 'w') as xml_file:
                    xml_file.write(entry.xml.encode('utf-8'))

    def write_bottom(self):
        pass
