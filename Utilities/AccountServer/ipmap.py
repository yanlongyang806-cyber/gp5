import GeoIP
import sys
from google_map import get_map_html

def get_locs(ips):
    gi = GeoIP.open("GeoLiteCity.dat", GeoIP.GEOIP_MEMORY_CACHE)

    locs = {}
    for ip in ips:
        if not ip:
            continue
        if ip in locs:
            locs[ip]['count'] += 1
        else:
            loc = gi.record_by_addr(ip)
            loc['count'] = 1
            locs[ip] = loc
    return locs

if __name__ == '__main__':
    ips = open(sys.argv[1], 'rb').read().split('\n')

    locs = get_locs(ips)
    print get_map_html(locs)

