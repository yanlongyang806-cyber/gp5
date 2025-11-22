<?cs include:"headerxhtml.cs" ?>

<!-- Utility Functions -->

<script type="text/javascript">
//<![CDATA[

// Number of additional fields created, for unique identifiers
var button_count = 0

// Create another entry field
function MoreButton(id, base_name, default_value) {
	button_count = button_count + 1
	$('<div><label class="morelabel" for="' + base_name + button_count
		+ '">... or</label><input id="' + base_name + button_count
		+ '" type="text" name="' + base_name
		+ '" value="' + default_value
		+ '"></input></div>').appendTo("#" + id)
}

// Number of additional revision field sets created, for unique identifiers
var rev_button_count = 0

// Create another entry field, for Revisions
function MoreButtonRevision(id, base_name) {
	rev_button_count = rev_button_count + 1
	var prefix = base_name + rev_button_count
	$('<div><label class="simplelabel" for="' + prefix + 'Rev">Revision</label><input type="text" name="' + base_name + 'Rev" id="' + prefix + 'Rev" size="6"></input> ' +
		'<label for="' + prefix + 'weight">Weight</label><input type="text" name="' + base_name + 'Weight" id="' + prefix + 'Weight" value="1" size="1"></input></div>').appendTo("#" + id)
}

//]]>
</script>

<!-- Header -->

<h1><a href="/"><?cs var:Server_Display_Name ?></a> - <?cs var:Title ?></h1>

<!-- Existing Rules -->

<h2>Current Rules</h2>

<?cs include:"display_autoupconf.cs" ?>

<!-- Adding New Rules -->

<h2><a name="add_autoup_rule"></a>Add Autoupdate Rule</h2>
<p>An Autoupdate rule is used to match against an Autoupdate token provided by a client patching to a specific revision of that Autoupdate project.
The default behavior when there are no Autoupdate rules is to always tell the client to Autoupdate to the latest revision.</p>
<div><br /></div>
<form method="post" action="autoupconf/add_autoup_rule">
	<fieldset><legend>Match Conditions</legend>
		<label class="simplelabel" for="Token">Token</label><input type="text" name="autoupToken" id="Token" value="<?cs var:Token ?>"></input>
		<input type="button" value="Add another token" onclick="MoreButton('tokenlist', 'autoupToken', '')"></input>
		<div id="tokenlist"></div>
		<div class="formdetails">This rule will apply to clients using any of these Autoupdate tokens.</div>

		<hr />

		<label class="simplelabel" for="Category">Category</label><input type="text" name="autoupCategory" id="Category" value="<?cs var:Category ?>"></input>
		<input type="button" value="Add another category" onclick="MoreButton('categorylist', 'autoupCategory', '')"></input>
		<div id="categorylist"></div>
		<div class="formdetails">This rule will apply to any servers matching one of these categories.  If left blank, this rule will apply to all categories.</div>

		<hr />

		<label class="simplelabel" for="IP">IP mask</label><input type="text" name="autoupIP" id="IP" value="*.*.*.*"></input>
		<input type="button" value="Add another IP mask" onclick="MoreButton('iplist', 'autoupIP', '*.*.*.*')"></input>
		<div id="iplist"></div>
		<div class="formdetails">This rule will apply to any clients with IP addresses that match one of these masks.</div>
	</fieldset><div><br /></div>

	<fieldset><legend>Revision(s)</legend>
		<label class="simplelabel" for="Rev">Revision</label><input type="text" name="revisionRev" id="Rev" size="6"></input>
		<label for="Weight">Weight</label><input type="text" name="revisionWeight" id="Weight" value="1" size="1"></input>
        <input type="button" value="Add another revision" onclick="MoreButtonRevision('revisionlist', 'revision')"></input>
		<div id="revisionlist"></div>
		<div class="formdetails">When a client matches this rule, it will use one of these revisions for its Autoupdate file.  The probability of being
		directed to each revision is proportional to the associated weight.</div>
	</fieldset><div><br /></div>

	<fieldset style="border: none">
	<input class="createbutton" type="submit" value="Create Autoupdate Rule"></input>
	When all of the rule details have been filled in, select "Create Autoupdate Rule" to validate and verify the rule.
	</fieldset>

</form>

<?cs include:"footer.cs" ?>
