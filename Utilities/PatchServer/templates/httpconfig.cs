<?cs include:"headerxhtml.cs" ?>



<!-- Utility Functions -->

<script type="text/javascript">
//<![CDATA[

// Number of additional fields created, for unique identifiers
var button_count = 0

// Same, but for view rules
var view_button_count = 0

// Same, but for branch rules
var branch_button_count = 0

// Create another entry field
function MoreButton(id, base_name, default_value) {
	button_count = button_count + 1
	$('<div><label class="morelabel" for="' + base_name + button_count
		+ '">... or</label><input id="' + base_name + button_count
		+ '" type="text" name="' + base_name
		+ '" value="' + default_value
		+ '"></input></div>').appendTo("#" + id)
}

// Create another entry field, for HTTP info
function MoreButtonServerView(id, base_name) {
	view_button_count = view_button_count + 1
	var prefix = base_name + view_button_count
	$('<div><label class="morelabel" for="' + prefix + 'hostname">Hostname</label><input type="text" name="' + base_name + 'hostname" id="' + prefix + 'hostname" size="25"></input> ' +
		'<label for="' + prefix + 'port">Port</label><input type="text" name="' + base_name + 'port" id="' + prefix + 'port" value="80" size="6"></input> ' +
		'<label for="' + prefix + 'prefix">Prefix</label><input type="text" name="' + base_name + 'prefix" id="' + prefix + 'prefix" size="25"></input> ' +
		'<label for="' + prefix + 'weight">Weight</label><input type="text" name="' + base_name + 'weight" id="' + prefix + 'weight" value="1" size="1"></input> ' +
		'<label for="' + prefix + 'loadbalancer">Load balancer?</label><input type="checkbox" name="' + base_name + 'loadbalancer' + view_button_count + '" id="' + prefix + 'loadbalancer"></input></div>').appendTo("#" + id)
}

// Create another entry field, for HTTP info
function MoreButtonServerBranch(id, base_name) {
	branch_button_count = branch_button_count + 1
	var prefix = base_name + branch_button_count
	$('<div><label class="morelabel" for="' + prefix + 'hostname">Hostname</label><input type="text" name="' + base_name + 'hostname" id="' + prefix + 'hostname" size="25"></input> ' +
		'<label for="' + prefix + 'port">Port</label><input type="text" name="' + base_name + 'port" id="' + prefix + 'port" value="80" size="6"></input> ' +
		'<label for="' + prefix + 'prefix">Prefix</label><input type="text" name="' + base_name + 'prefix" id="' + prefix + 'prefix" size="25"></input> ' +
		'<label for="' + prefix + 'weight">Weight</label><input type="text" name="' + base_name + 'weight" id="' + prefix + 'weight" value="1" size="1"></input> ' +
		'<label for="' + prefix + 'loadbalancer">Load balancer?</label><input type="checkbox" name="' + base_name + 'loadbalancer' + branch_button_count + '" id="' + prefix + 'loadbalancer"></input></div>').appendTo("#" + id)
}

//]]>
</script>



<!-- Header -->

<h1><a href="/"><?cs var:Server_Display_Name ?></a> - <?cs var:Title ?></h1>



<!-- Existing Rules -->

<h2>Current Rules</h2>

<?cs include:"display_rules.cs" ?>



<!-- Adding New Rules -->

<h2><a name="add_namedview_rule"></a>Add Named View Rule</h2>
<p>A named view rule is used to match against a client patching to a specific named view, which is usually a particular build that has been
individually copied to a mirror webserver to supplement capacity.</p>
<div><br /></div>
<form method="post" action="httpconfig/add_namedview_rule">

	<fieldset><legend>Match Conditions</legend>

		<label class="simplelabel" for="viewcategories">Server category</label><input type="text" name="viewcategories" id="viewcategories"></input>
		<input type="button" value="Add another server category" onclick="MoreButton('viewcategorylist', 'viewcategories', '')"></input>
		<div id="viewcategorylist"></div>
		<div class="formdetails">This rule will apply to any servers matching one of these categories.  If left blank, this rule will apply to all categories.</div>

		<label class="simplelabel" for="viewips">IP mask</label><input type="text" name="viewips" id="viewips" value="*.*.*.*"></input>
		<input type="button" value="Add another IP mask" onclick="MoreButton('viewiplist', 'viewips', '*.*.*.*')"></input>
		<div id="viewiplist"></div>
		<div class="formdetails">This rule will apply to any clients with IP addresses that match one of these masks.</div>

		<hr />
		<label class="simplelabel" for="viewprojects">Project name</label><input type="text" name="viewprojects" id="viewprojects" value="<?cs var:Viewprojects ?>"></input>
		<input type="button" value="Add another project name" onclick="MoreButton('viewprojectlist', 'viewprojects', '')"></input>
		<div id="viewprojectlist"></div>
		<div class="formdetails">This rule will apply to clients who set their view to one of these projects.</div>

		<hr />
		<label class="simplelabel" for="viewname">View name</label><input type="text" name="viewname" id="viewname" value="<?cs var:Viewname ?>"></input>
		<div class="formdetails">This rule will apply to clients who set their view to this specific name.</div>

	</fieldset><div><br /></div>

	<fieldset><legend>Source Servers</legend>
		<label class="simplelabel" for="viewhostname">Hostname</label><input type="text" name="viewhostname" id="viewhostname" size="25"></input>
		<label for="viewport">Port</label><input type="text" name="viewport" id="viewport" value="80" size="6"></input>
		<label for="viewprefix">Prefix</label><input type="text" name="viewprefix" id="viewprefix" size="25"></input>
		<label for="viewweight">Weight</label><input type="text" name="viewweight" id="viewweight" value="1" size="1"></input>
        <label for="viewloadbalancer">Load Balancer?</label><input type="checkbox" name="viewloadbalancer0" id="viewloadbalancer"></input>
		<input type="button" value="Add another server" onclick="MoreButtonServerView('viewserverlist', 'view')"></input>
		<div id="viewserverlist"></div>
		<div class="formdetails">If a client matches this rule, it will be directed to one of the listed servers.  The probability of
		being directed to each server is proportional to the associated weight.</div>
	</fieldset><div><br /></div>

	<fieldset style="border: none">
	<input class="createbutton" type="submit" value="Create Named View Rule"></input>
	When all of the rule details have been filled in, select "Create Named View Rule" to validate and verify the rule.
	</fieldset>

</form>

<h2><a name="add_branch_rule"></a>Add Branch Rule</h2>
<p>A branch rule is used to match against a set of branches for a set of projects.  Typically, this is used to offload traffic to a standing, automatic mirror.</p>
<div><br /></div>
<form method="post" action="httpconfig/add_branch_rule">

	<fieldset><legend>Match Conditions</legend>

		<label class="simplelabel" for="branchcategories">Server category</label><input type="text" name="branchcategories" id="branchcategories"></input>
		<input type="button" value="Add another server category" onclick="MoreButton('branchcategorylist', 'branchcategories', '')"></input>
		<div id="branchcategorylist"></div>
		<div class="formdetails">This rule will apply to any servers matching one of these categories.  If left blank, this rule will apply to all categories.</div>

		<label class="simplelabel" for="branchips">IP mask</label><input type="text" name="branchips" id="branchips" value="*.*.*.*"></input>
		<input type="button" value="Add another IP mask" onclick="MoreButton('branchiplist', 'branchips', '*.*.*.*')"></input>
		<div id="branchiplist"></div>
		<div class="formdetails">This rule will apply to any clients with IP addresses that match one of these masks.</div>

		<hr />
		<label class="simplelabel" for="branchprojects">Project name</label><input type="text" name="branchprojects" id="branchprojects" value="<?cs var:Branchprojects ?>"></input>
		<input type="button" value="Add another project name" onclick="MoreButton('branchprojectlist', 'branchprojects', '')"></input>
		<div id="branchprojectlist"></div>
		<div class="formdetails">This rule will apply to clients who set their view to one of these projects.</div>

		<hr />
		<label class="simplelabel" for="branchnumbers">Branch number</label><input type="text" name="branchnumbers" id="branchnumbers" value="<?cs var:Branchnumbers ?>"></input>
		<input type="button" value="Add another branch number" onclick="MoreButton('branchnumberlist', 'branchnumbers', '')"></input>
		<div id="branchnumberlist"></div>
		<div class="formdetails">This rule will apply to clients who set their view to one of these projects.</div>

	</fieldset><div><br /></div>

	<fieldset><legend>Source Servers</legend>
		<label class="simplelabel" for="viewhostname">Hostname</label><input type="text" name="branchhostname" id="branchhostname" size="25"></input>
		<label for="branchport">Port</label><input type="text" name="branchport" id="branchport" value="80" size="6"></input>
		<label for="branchprefix">Prefix</label><input type="text" name="branchprefix" id="branchprefix" size="25"></input>
		<label for="branchweight">Weight</label><input type="text" name="branchweight" id="branchweight" value="1" size="1"></input>
        <label for="branchloadbalancer">Load balancer?</label><input type="checkbox" name="branchloadbalancer0" id="branchloadbalancer"></input>
		<input type="button" value="Add another server" onclick="MoreButtonServerBranch('branchserverlist', 'branch')"></input>
		<div id="branchserverlist"></div>
		<div class="formdetails">If a client matches this rule, it will be directed to one of the listed servers.  The probability of
		being directed to each server is proportional to the associated weight.</div>
	</fieldset><div><br /></div>

	<fieldset style="border: none">
	<input class="createbutton" type="submit" value="Create Branch Rule"></input>
	When all of the rule details have been filled in, select "Create Branch Rule" to validate and verify the rule.
	</fieldset>

</form>



<?cs include:"footer.cs" ?>
