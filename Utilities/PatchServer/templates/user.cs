<?cs include:"header.cs" ?>
<?cs include:"macros.cs" ?>
<h1><a href="/"><?cs var:Config.Serverdisplayname ?></a> - User <?cs var:User ?></h1>

<fieldset>
  <legend>Checkouts</legend>
  <ul>
    <?cs each:co = Checkouts ?><li>Branch <?cs var:co.Co.Branch ?> - <a href="/<?cs var:Serverdb.Name ?>/file/<?cs var:co.Path ?>/"><?cs var:co.Path ?></a></li>
    <?cs /each ?>
  </ul>
</fieldset>

<?cs include:"footer.cs" ?>
