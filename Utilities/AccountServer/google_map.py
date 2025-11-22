def get_map_html(locs):
    html = """
    <!DOCTYPE html>
    <html>
      <head>
        <meta name="viewport" content="initial-scale=1.0, user-scalable=no" />
        <style type="text/css">
          html { height: 100% }
          body { height: 100%; margin: 0; padding: 0 }
          #map_canvas { height: 100% }
        </style>
        <script type="text/javascript"
          src="http://maps.googleapis.com/maps/api/js?sensor=false">
        </script>
        <script type="text/javascript">
          function initialize() {
            var myOptions = {
              center: new google.maps.LatLng(0, 0),
              zoom: 3,
              mapTypeId: google.maps.MapTypeId.ROADMAP
            };
            var map = new google.maps.Map(document.getElementById("map_canvas"),
                myOptions);
    """

    for ip, loc in locs.iteritems():
        marker = """
            var marker = new google.maps.Marker({
                map: map,
                visible: true,
                title: "TITLE",
                position: new google.maps.LatLng(LAT_LNG)
            });
        """
        marker = marker.replace('LAT_LNG',
                str(loc['latitude']) + ',' + str(loc['longitude']))
        marker = marker.replace('TITLE', ip + ' (' + str(loc['count']) + ')')
        html += marker

    html += """
          }
        </script>
      </head>
      <body onload="initialize()">
        <div id="map_canvas" style="width:100%; height:100%"></div>
      </body>
    </html>
    """
    return html
