<html>
  <head>
    <title>Test</title>
    <style type="text/css">
    body { background: #eee; color: black; font-family: Verdana, sans-serif; }
    p { margin: 0 }
    ul { margin: 0 auto 0.5em 2em; padding: 0; }
    li { padding-bottom: 0.3em; }
    table { vertical-align: middle; border-spacing: 0; border-collapse: collapse; margin: 0 auto 1em 0; }
    tr { border: 0; }
	tr.disabled { background-color: #ffaaaa; }
    td, th { padding: 2px 6px; vertical-align: top; white-space: nowrap; }
    td.comment { padding: 2px 6px; vertical-align: top; white-space: normal; }
    form { margin: 0; padding: 0; border: 0; }
    a:link    { color: #00f; text-decoration: none; }
    a:visited { color: #00a; text-decoration: none; }
    a:hover   { color: #00f; text-decoration: underline; }
    a.new:link    { color: #0c0; text-decoration: none; }
    a.new:visited { color: #080; text-decoration: none; }
    a.new:hover   { color: #0c0; text-decoration: underline; }
    a.del:link    { color: #f00; text-decoration: none; }
    a.del:visited { color: #a00; text-decoration: none; }
    a.del:hover   { color: #f00; text-decoration: underline; }
    .table { border: solid #000 1px; border-spacing: 0; }
    .rowh { background: #fff; font-weight: bold; text-align: center; }
    .row0 { background: #eee; }
    .row1 { background: #ddd; }
    .rowdeleted0 { background: #fdd; }
    .rowdeleted1 { background: #ecc; }
    .rowf { background: #fff; }
    .time { font-family: monospace; font-size: smaller; vertical-align: middle; white-space: nowrap; }
    </style>
  </head>
  <body>
    <p><strong>PatchServer: <?cs var:Config.Serverdisplayname ?></strong></p>
    <p>Machine: <?cs var:Machine_Name ?> ( <?cs var:Public_Ip ?> <?cs if:Public_Ip != Local_Ip ?>/ <?cs var:Local_Ip ?><?cs /if ?> )</p>
    <p>Stats: <?cs var:Status ?></p>
    <?cs if:Config.Parent.Server ?>
      <p>Mirror Parent: <b>
        <a href="http://<?cs var:Config.Parent.Server ?>/"><?cs var:Config.Parent.Server ?></a>
        <?cs if:Config.Parent.Port != 0?>:<?cs var:Config.Parent.Port ?><?cs /if ?>
      </b></p>
      <?cs each:mirconf = Config.MirrorConfig ?>
        <p>Mirroring: <?cs var:mirconf.Db ?> (<?cs each:mirbr = mirconf.BranchToMirror ?>Branch <?cs var:mirbr.Branch ?> from rev <?cs var:mirbr.Startatrevision ?><?cs if:!last(mirbr) ?>, <?cs /if ?><?cs /each ?>)</p>
      <?cs /each ?>
      Mirroring status: <?cs var:Update_Status ?>
    <?cs /if ?>
    <?cs each:child = Child_Links ?>
      <p>Mirror Child: <b><a href="http://<?cs var:child.Ipstr ?>/"><?cs var:child.Ipstr ?></a></b> (<?cs if:child.Notify_Me ?>waiting<?cs else ?>updating<?cs /if ?>)</p>
    <?cs /each ?>

  </body>
</html>