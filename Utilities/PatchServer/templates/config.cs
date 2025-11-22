<?cs include:"header.cs" ?>
<h1><a href="/"><?cs var:Config.Serverdisplayname ?></a> - Configuration</h1>
<form method="post" />
  <?cs if:Config.Bandwidthconfig.Total ?><fieldset style="width: 50%; margin-bottom: 5px;">
    <legend>Bandwidth</legend>
    <div>
      <label for="bandwidth_total">Total:</label>
      <input name="bandwidth_total" id="bandwidth_total" value="<?cs var:Config.Bandwidthconfig.Total ?>" />
    </div>
    <div>
      <label for="bandwidth_peruser">Per user:</label>
      <input name="bandwidth_peruser" id="bandwidth_peruser" value="<?cs var:Config.Bandwidthconfig.Peruser ?>" />
    </div>
    <div>
      <label for="bandwidth_timeslice">Time slice:</label>
      <input name="bandwidth_timeslice" id="bandwidth_timeslice" value="<?cs var:Config.Bandwidthconfig.Timeslice ?>" />
    </div>
  </fieldset><?cs /if ?>
  <div>
    <hr>
    <label for="locked">Locked:</label>
    <input type="checkbox" name="locked" id="locked" value="1" <?cs if:Config.Locked ?>checked="checked"<?cs /if ?> />
	<div>If checked, don't allow new clients to connect.</div>
    <hr>
    <label for="down">Report as Down:</label>
    <input type="checkbox" name="down" id="down" value="1" <?cs if:Config.Reportasdown ?>checked="checked"<?cs /if ?> />
	<div>If checked, report to monitoring agents (such as a load balancer) that the server is down on the <a href="/monitor">status page</a>.</div>
    <hr>
  </div>
  <div class="buttons">
    <input type="submit" name="submit" value="Save" />
  </div>
</form>
<?cs include:"footer.cs" ?>
