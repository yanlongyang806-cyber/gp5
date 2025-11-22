"""Tool to toggle employee permission on large numbers of accounts"""
import sys
import urllib
import string


class AccountOpener():
    """Container class to group all the things in common"""
    def __init__(self, root_url):
        """This is a class constructor!"""
        self.base_url = root_url + "/accounts/view.html?"
        self.host = self.base_url.partition("//")[2]
        self.suffix1 = "id=&name="
        self.suffix2 = "&display_name=&guid=&email=&pwe_name=&pwe_email="
        self.door = urllib.FancyURLopener()

    def toggle(self, name):
        """Takes a name as argument, toggles employee status for each"""
        id_line = None
        initial = None
        resolution = None
        url = self.base_url + self.suffix1 + name + self.suffix2
        page = self.door.open(url)
        page_lines = page.read()
        page.close()
        page_lines = page_lines.split("\n")
        for line in page_lines:
            if "Account ID:" in line:
                id_line = line
            elif "Employee Status:" in line:
                initial = line
            if id_line !=None and initial != None:
                break
        if id_line == None or initial == None:
            print "id_line\t" + str(id_line)
            print "initial\t" + str(initial)
            return False
        id_line = id_line.split(" ")
        acct_id = id_line[-1]
        data = {"id" : acct_id, "toggleEmployeeStatus" : "Toggle"}
        data = urllib.urlencode(data)
        resolution = self.door.open(url, data)
        resolution_lines = resolution.read()
        resolution.close()
        resolution_lines = resolution_lines.split("\n")
        for line in resolution_lines:
            if "Employee Status:" in line:
                resolution = line
                break
        if resolution == None:
            print "resolution\t" + str(resolution)
            return False
        return resolution != initial


if __name__ == "__main__":
    """checks args, opens url, reads file, toggles each name"""
    if len(sys.argv) < 3:
        print "Insufficient args:"
        print "\ttoggle_employee_status.py <url> <file location>"
        print "where <file location> is a list of account names."
        sys.exit(1)
    try:
        opener = AccountOpener(sys.argv[1])
        f = open(sys.argv[2], 'r')
        lines = f.readlines()
        f.close()
        for line in lines:
            line = line.strip()
            if not opener.toggle(line):
                print "%s failed!" % (line)
    except: sys.exit(2)
    else:
        print "Done!"
