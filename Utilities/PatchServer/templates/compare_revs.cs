<html>
  <head>
    <title><?cs var:Config.Serverdisplayname ?>: <?cs var:Serverdb.Name ?>: Compare Revisions</title>
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
  <?cs if:Error ?>
  <?cs var:Error ?>
  <pre>Use /CompareRevs?r=100&r=101
  Other params:
  allFiles=1 (show all files, not just diffs)
  count=# (how many files to show)
  start=# (which file to start at)
  sort=[file/r#] (sort by file or one of the revisions by size)</pre>
  <?cs else ?>
  <p>
    <a href="/">PatchServer</a> - <a href="/<?cs var:Serverdb.Name ?>/"><?cs var:Serverdb.Name ?></a> - Compare Revisions
  </p>
  <p>
    Project: <span style="font-size: 80%;">
    <?cs if:?Project ?>
      <a href="<?cs var:Url_Base ?>&project=">All Files</a>
    <?cs else ?>
      <strong>All Files</strong>
    <?cs /if ?>
  </p>
  <?cs /if ?>
  </body>
 </html>