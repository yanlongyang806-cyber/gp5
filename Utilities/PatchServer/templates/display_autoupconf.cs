<table class="table">

<!-- Print Autoupdate Config -->

<?cs each:rule = Autoup_Rules ?>

<tr class="rowh"><td colspan="3"><b>Autoupdate Rule</b></td></tr>

<?cs if:rule.Disabled == 1 ?>
	<tr class="disabled">
		<td colspan="3"><strong>Disabled</strong></td>
	</tr>
<?cs /if ?>

<?cs if:Controls == 1 ?>
	<?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
		<td colspan="3">
			<form method="post" action="autoupconf/edit_autoup_rule">
			<div>
			<input type="hidden" name="id" value="<?cs var:rule.ID ?>"></input>
			<?cs if:rule.Disabled == 1 ?>
				<input type="submit" name="enable" value="Enable"></input>
			<?cs else ?>
				<input type="submit" name="disable" value="Disable"></input>
			<?cs /if ?>
			<input type="submit" name="delete" value="Delete..."></input>
			</div>
			</form>
		</td>
	</tr>
<?cs /if ?>

<?cs each:Token = rule.Tokens ?>
	<?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
		<td>Token</td><td colspan="2"><b><?cs var:Token ?></b></td>
	</tr>
<?cs /each ?>

<?cs each:Category = rule.Categories ?>
    <?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
		<td>Category</td><td colspan="2"><?cs var:Category ?></td>
	</tr>
<?cs /each ?>

<?cs each:allowip = rule.Allowip ?>
	<?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
		<td>Allow IP</td><td colspan="2"><?cs var:allowip.Ip_Str ?></td>
	</tr>
<?cs /each ?>

<?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?><td colspan="3">

<?cs if:Controls == 1 ?>
	<form method="post" action="autoupconf/edit_autoup_rule">
		<div>
			<input type="hidden" name="id" value="<?cs var:rule.ID ?>"></input>
<?cs /if ?>

<table class="table">

<?cs each:Weightedrev = rule.Weightedrev ?>
	<tr><td valign="middle">Revision</td>
		<?cs if:Controls == 1 ?>
			<td><input  type="text" name="revisionRev" value="<?cs var:Weightedrev.Rev ?>" size="6"></input></td>
			<td><input  type="text" name="revisionWeight" value="<?cs var:Weightedrev.Weight ?>" size="6"></input></td>
		<?cs else ?>
			<td><?cs var:Weightedrev.Rev ?></td>
			<td>(<?cs var:Weightedrev.Weight ?>)</td>
		<?cs /if ?>
	</tr>
<?cs /each ?>

<?cs if:Controls == 1 ?>
	<tr>
		<td colspan="2"></td>
		<td>
			<input type="submit" name="Revisions" value="Save"></input>
		</td>
	</tr>
<?cs /if ?>

</table>

<?cs if:Controls == 1 ?>
	</div></form>
<?cs /if ?>

</td></tr>

<?cs /each ?>

</table>
