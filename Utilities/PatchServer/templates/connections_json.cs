<?cs include:"macros.cs" ?>
{<?cs each:client = Clients ?>
  '<?cs var:client.UID ?>': {
    client: '<?cs var:client.Ipstr ?>',
    project: '<?cs var:client.Project.Name ?>',
    view: '<?cs var:client.View_Name ?>',
    token: '<?cs var:client.Autoupdate_Token ?>',
    connected: '<?cs call:format_time(Current_Time - client.Start_Time) ?>',
    bucket: '<?cs call:format_bytes(client.Bucket) ?>',
    transfer: '<?cs call:format_bytes(Link_Stats[name(client)].Send.Real_Bytes) ?>',
    speed: '<?cs call:format_bytes(Link_Stats[name(client)].Send.Real_Bytes / (Current_Time - client.Start_Time)) ?>ps'
}<?cs if:!last(client) ?>,<?cs /if ?><?cs /each ?>}