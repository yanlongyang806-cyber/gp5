<?cs include:"header.cs" ?>
<h1><a href="/"><?cs var:Config.Serverdisplayname ?></a> - Redirects</h1>
<form method="post" />
  <?cs each:redir = Config.Redirectto ?><fieldset style="width: 50%; margin-bottom: 5px;">
    <legend><?cs var:redir.Direct_To.Server ?><?cs if:redir.Direct_To.Port ?>:<?cs var:redir.Direct_To.Port ?><?cs /if ?></legend>
    <div>
      <label for="disable_<?cs name:redir ?>">Disabled:</label>
      <input type="checkbox" name="disable_<?cs name:redir ?>" id="disable_<?cs name:redir ?>" value="1" <?cs if:redir.Disabled ?>checked="checked"<?cs /if ?> />
    </div>
    <fieldset style="float: left; clear: both; margin-top: 7px;">
      <legend>IPs</legend>
      <ul style="margin: 0; list-style: none;">
        <?cs each:ip = redir.Ip ?>
        <li><?cs var:ip.Ip_Str ?></li>
        <?cs /each ?>
      </ul>
    </fieldset>
    <?cs if:subcount(redir.Alternative) ?>
    <fieldset style="float: left; clear: both; margin-top: 7px;">
      <legend>Alternatives</legend>
      <?cs each:alt = redir.Alternative ?><div>
        <label for="disable_<?cs name:redir ?>_<?cs name:alt ?>"><?cs var:alt.Address.Server ?><?cs if:alt.Address.Port ?>:<?cs var:alt.Address.Port ?><?cs /if ?></label>
        <input type="checkbox" name="disable_<?cs name:redir ?>_<?cs name:alt ?>" id="disable_<?cs name:redir ?>_<?cs name:alt ?>" value="1" <?cs if:alt.Disabled ?>checked="checked"<?cs /if ?> />
      </div><?cs /each ?>
    </fieldset>
    <?cs /if ?> 
  </fieldset><?cs /each ?>
  <div class="buttons">
    <input type="submit" name="submit" value="Save" />
  </div>
</form>
<?cs include:"footer.cs" ?>