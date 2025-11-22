<table class="table">



<!-- Print Named View Rules -->

<?cs each:rule = Namedview_Rules ?>

<tr class="rowh"><td colspan="3"><b>Named View Rule</b></td></tr>

<?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
	<td>View Name</td><td colspan="2"><b><?cs var:rule.Name ?></b></td>
</tr>

<?cs if:rule.Disabled == 1 ?><tr class="disabled"><td colspan="3"><strong>Disabled</strong></td></tr><?cs /if ?>

<?cs if:Controls == 1 ?>
	<?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
		<td colspan="3">
			<form method="post" action="httpconfig/edit_namedview_rule">
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

<?cs each:project = rule.Project ?>
	<?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
		<td>Project</td><td colspan="2"><?cs var:project ?></td>
	</tr>
<?cs /each ?>

<?cs each:category = rule.Categories ?>
	<?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
		<td>Category</td><td colspan="2"><?cs var:category ?></td>
	</tr>
<?cs /each ?>

<?cs each:token = rule.Tokens ?>
	<?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
		<td>Token</td><td colspan="2"><?cs var:token ?></td>
	</tr>
<?cs /each ?>

<?cs each:allowip = rule.Allowip ?>
	<?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
		<td>Allow IP</td><td colspan="2"><?cs var:allowip.Ip_Str ?></td>
	</tr>
<?cs /each ?>

<?cs each:httpinfo = rule.Httpinfo ?>
    <?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
		<td>HTTP Server</td><td>"<?cs var:httpinfo.Info ?>"</td><td>(<?cs var:httpinfo.Weight ?>)
		<?cs if:httpinfo.Load_Balancer == 1 ?>(Load balancer)<?cs else ?>(Simple)<?cs /if ?>
		</td>
	</tr>
<?cs /each ?>

<?cs /each ?>

<!-- Print Branch Rules -->

<?cs each:rule = Branch_Rules ?>

<tr class="rowh"><td colspan="3"><b>Branch Rule</b></td></tr>

<?cs each:project = rule.Project ?>
	<?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
		<td>Project</td><td colspan="2"><?cs var:project ?></td>
	</tr>
<?cs /each ?>

<?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
	<td>Branches</td><td colspan="2"><b><?cs each:branch = rule.Branch ?><?cs var:branch ?> <?cs /each ?></b></td>
</tr>

<?cs if:rule.Disabled == 1 ?><tr class="disabled"><td colspan="3"><strong>Disabled</strong></td></tr><?cs /if ?>

<?cs if:Controls == 1 ?>
	<?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
		<td colspan="3">
			<form method="post" action="httpconfig/edit_branch_rule">
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

<?cs each:category = rule.Categories ?>
	<?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
		<td>Category</td><td colspan="2"><?cs var:category ?></td>
	</tr>
<?cs /each ?>

<?cs each:token = rule.Tokens ?>
	<?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
		<td>Token</td><td colspan="2"><?cs var:token ?></td>
	</tr>
<?cs /each ?>

<?cs each:allowip = rule.Allowip ?>
	<?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
		<td>Allow IP</td><td colspan="2"><?cs var:allowip.Ip_Str ?></td>
	</tr>
<?cs /each ?>

<?cs each:httpinfo = rule.Httpinfo ?>
    <?cs if:rule.Disabled == 1 ?><tr class="disabled"><?cs else ?><tr><?cs /if ?>
		<td>HTTP Server</td><td>"<?cs var:httpinfo.Info ?>"</td><td>(<?cs var:httpinfo.Weight ?>)
	    <?cs if:httpinfo.Load_Balancer == 1 ?>(Load balancer)<?cs else ?>(Simple)<?cs /if ?>
		</td>
	</tr>
<?cs /each ?>

<?cs /each ?>

</table>
