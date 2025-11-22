<?cs include:"header.cs" ?>
<?cs include:"macros.cs" ?>
<h1><a href="/"><?cs var:Config.Serverdisplayname ?></a> - Connections</h1>

<form method="post" style="width: 30em;">
  <fieldset>
    <legend>Options</legend>
    <div>
      <label for="enable_refresh">Enable refresh: </label>
      <input type="checkbox" id="enable_refresh" name="enable_refresh" value="1" />
    </div>
  </fieldset>
</form>

<p style="margin: 4px 0;">
  Clients: <span id="client_count"><?cs var:subcount(Clients) ?></span>
  Bucket: <span id="global_bucket"><?cs call:format_bytes(Global_Bucket) ?></span>
  (<span id="global_percent"><?cs var:Global_Percent ?></span>%)
  Throttled: <span id="throttled_requests"><?cs var:Throttled_Requests ?></span>
  Speed: <span id="avg_speed"><?cs call:format_bytes(Avg_Speed) ?>ps</span>
</p>

<style>
  #clients {
    border-collapse: collapse;
    border: 1px solid black;
  }
  #clients td, #clients th {
    border: 1px solid black;
  }
  #clients tbody tr:hover {
    background-color: #AAAAFF;
  }
</style>

<table id="clients">
  <thead>
    <tr>
      <th>Client</th>
      <th>Project</th>
      <th>View</th>
      <th>Token</th>
      <th>Connected</th>
      <th>Bucket</th>
      <th colspan="2">Transfer</th>
      <th colspan="2">Rate</th>
    </tr>
  </thead>
  <tbody>
    <?cs each:client = Clients ?><tr id="client<?cs var:client.UID ?>">
      <td><?cs var:client.Ipstr ?></td> 
      <td><?cs var:client.Project.Name ?></td> 
      <td><?cs var:client.View_Name ?></td> 
      <td><?cs var:client.Autoupdate_Token ?></td> 
      <td><?cs call:format_time(Current_Time - client.Start_Time) ?></td> 
      <td><?cs call:format_bytes(client.Bucket) ?></td>
      <td><?cs call:format_bytes(Link_Stats[name(client)].Send.Real_Bytes) ?></td> 
      <td><?cs call:format_bits_from_bytes(Link_Stats[name(client)].Send.Real_Bytes) ?></td> 
      <td><?cs call:format_bytes(Link_Stats[name(client)].Send.Real_Bytes / (Current_Time - client.Start_Time)) ?>ps</td>
      <td><?cs call:format_bits_from_bytes(Link_Stats[name(client)].Send.Real_Bytes / (Current_Time - client.Start_Time)) ?>ps</td>
    </tr><?cs /each ?>
  </tbody>
</table>

<script type="text/javascript">
$(function() {
  var headers = [];
  $('#clients th').each(function(i) {
    headers[i] = $(this).text().toLowerCase();
  });
  window.headers = headers;

  var refresh_data = function() {
    $.ajax({
      url: '/connections?format=json',
      dataType: 'json',
      success: function(data) {
        var rows = [];
        $.each(data, function(uid, client) {
          var row = '';
          for(var n=0; n<headers.length; n++) {
            row += '<td>' + client[headers[n]] + '</td>';
          }
          rows.push({id:uid, html:'<tr>' + row + '</tr>'});
        });
        rows.sort(function(a, b) { return a.id - b.id; });
        var html = '';
        $.each(rows, function(i, row) {
          html += row.html;
        })
        $('#clients tbody').html(html);
        $('#client_count').text(rows.length);
      }
    });

    if($('#enable_refresh:checked').val() == '1')
      setTimeout(refresh_data, 10000);
  }
  if($('#enable_refresh:checked').val() == '1')
    setTimeout(refresh_data, 10000);
  $('#enable_refresh').change(function() {
    if($('#enable_refresh:checked').val() == '1')
      refresh_data();
  });
});
</script>
<?cs include:"footer.cs" ?>