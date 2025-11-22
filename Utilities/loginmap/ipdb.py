import csv
import sqlite3

# "ip_start";"country_code";"country_name";"region_code";"region_name";"city";"zipcode";"latitude";"longitude";"gmtOffset";"dstOffset"

def create_city():
    reader = csv.DictReader(open('ip_group_city.csv'), delimiter=';')
    db = sqlite3.connect('geoip.db')
    cursor = db.cursor()
    cursor.execute('''CREATE TABLE IF NOT EXISTS ip_group_city (
        ip_start INTEGER PRIMARY KEY,
        country_code TEXT,
        country_name TEXT,
        region_code TEXT,
        region_name TEXT,
        city TEXT,
        zipcode TEXT,
        latitude TEXT,
        longitude TEXT,
        gmtOffset TEXT,
        dstOffset TEXT
    )''')
    n = 0
    for row in reader:
        keynames = ','.join(row.iterkeys())
        qmarks = ','.join(['?']*len(row))
        cursor.execute('INSERT INTO ip_group_city (%s) VALUES (%s)'%(keynames, qmarks), row.values())
        n += 1
        if n % 10000 == 0:
            print n
    db.commit()
    
# "ip_start";"ip_cidr";"country_code";"country_name"
    
def create_country():
    reader = csv.DictReader(open('ip_group_country.csv'), delimiter=';')
    db = sqlite3.connect('geoip.db')
    cursor = db.cursor()
    cursor.execute('''CREATE TABLE IF NOT EXISTS ip_group_country (
        ip_start INTEGER PRIMARY KEY,
        ip_cidr TEXT,
        country_code TEXT,
        country_name TEXT
    )''')
    n = 0
    for row in reader:
        keynames = ','.join(row.iterkeys())
        qmarks = ','.join(['?']*len(row))
        cursor.execute('INSERT INTO ip_group_country (%s) VALUES (%s)'%(keynames, qmarks), row.values())
        n += 1
        if n % 10000 == 0:
            print n
    db.commit()

def main():
    create_city()
    create_country()
    
if __name__ == '__main__':
    main()