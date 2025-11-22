rollout cryptScaling "Cryptic AR - Scaling" width:472 height:528
(
	slider sldShoulders "Shoulders" pos:[24,32] width:206 height:44 range:[-100,100,0] controller:scl_shoulders
	slider sldHeight "Height" pos:[24,224] width:206 height:44 range:[-100,100,0] controller:scl_height
	slider sldArms "Arm Length" pos:[24,272] width:206 height:44 range:[-100,100,0] controller:scl_arms
	slider sldHands "Hands" pos:[24,416] width:206 height:44 range:[-100,100,0] controller:scl_hands
	slider sldHead "Head" pos:[24,368] width:206 height:44 range:[-100,100,0] controller:scl_head
	slider sldLegs "Leg Length" pos:[24,320] width:206 height:44 range:[-100,100,0] controller:scl_legs
	slider sldFeet "Feet" pos:[24,464] width:206 height:44 range:[-100,100,0] controller:scl_feet
	groupBox grp2 "Simple Scaling" pos:[8,8] width:248 height:512
	button btnReset "Reset" pos:[264,472] width:200 height:48
	dropDownList ddlPresets "Presets" pos:[264,8] width:200 height:40
	editText txtPresetName "" pos:[330,400] width:134 height:24
	button btnDelete "Delete" pos:[368,53] width:96 height:28
	button btnLoad "Import" pos:[264,432] width:96 height:32
	button btnSave "Export" pos:[368,432] width:96 height:32
	button btnApply "Apply" pos:[264,53] width:96 height:28
	slider sldMass "Mass" pos:[24,80] width:206 height:44 range:[-100,100,0] controller:scl_mass
	bitmap bmp1 "Bitmap" pos:[264,118] width:200 height:274 fileName:"logo.png"
	slider sldHips "Hips" pos:[24,176] width:206 height:44 range:[-100,100,50] controller:scl_hips
	slider sldFat "Fat" pos:[24,128] width:206 height:44 range:[-100,100,0] controller:scl_fat
	button btnAdd "Add" pos:[264,400] width:64 height:26
	button btnUpdate "Update" pos:[318,85] width:96 height:28
	
	function update_root_scale = (
		
		update_stream = stringStream ""
		
		for i = 1 to numPresets do( 		
		    format "%," presets[i][1] to:update_stream
		    for f = 1 to 9 do(
		 	      format "%," presets[i][2][f] to:update_stream			
			)
			format "%*" presets[i][2][10] to:update_stream
		)
		
		update_string = ""
		
		seek update_stream 0
		
		update_string = readLine update_stream
		
		SetUserProp $Props_Scale "ScalePresets" update_string
			
	)

	
	on btnReset pressed do(
	
		sldShoulders.value = 0
		sldHeight.value = 0
		sldArms.value = 0
		sldHands.value = 0
		sldHead.value = 0
		sldLegs.value = 0
		sldFeet.value = 0
		sldMass.value = 0
		sldHips.value = 0
		sldFat.value = 0
		
	)
	
	on btnAdd pressed do(
	
		numPresets = numPresets + 1
		
		append presets #(txtPresetName.text, #(
		sldShoulders.value,
		sldHeight.value,
		sldArms.value,
		sldHands.value,
		sldHead.value,
		sldLegs.value,
		sldFeet.value,
		sldMass.value,
		sldHips.value,
		sldFat.value))
	
		temparray = #()
		for i = 1 to numPresets do (
		
			append temparray presets[i][1]
		
		)
		ddlPresets.items = temparray
		ddlPresets.selection = numPresets
		
		update_root_scale()
	
	)
	
	on btnLoad pressed do (
	
		filename = getOpenFileName caption:"Select new scale file:" filename:"scales.pos"
		
		numPresets = 0	
		
		presets = #()
		
		try (
			in_file = openFile filename
			
			numPresets = readValue in_file
			
			for i = 1 to numPresets do(
			
				append presets #(readDelimitedString in_file ",", #(
			
							readValue in_file,
							readValue in_file,
							readValue in_file,
							readValue in_file,
							readValue in_file,
							readValue in_file,
							readValue in_file,
							readValue in_file,
							readValue in_file,
							readValue in_file))
						
				)
				
			close in_file
			
			temparray = #()
			
			for i = 1 to numPresets do (
			
				append temparray presets[i][1]
			
			)
			
			ddlPresets.items = temparray
			
			txtPresetName.text = presets[ddlPresets.selection][1]
			
			update_root_scale()
			
		)catch(
		--Do nothing

		)
	
	)
	
	on btnSave pressed do (
	
		file_name = getSaveFileName caption:"Select Export Destination:" filename:"scale.pos"
		try(
			out_file = createfile file_name
			
			format "%\n" numPresets to:out_file
			for i = 1 to numPresets do( 		
			    format "%," presets[i][1] to:out_file
			    for f = 1 to 9 do(
			 	      format "%," presets[i][2][f] to:out_file			
				)
				format "%\n" presets[i][2][10] to:out_file
			)
			close out_file
		)catch(
		
			--Do Nothing
		
		)
	
	)
	
	on btnUpdate pressed do (
	
		presets[ddlPresets.selection] = #(txtPresetName.text, #(
		sldShoulders.value,
		sldHeight.value,
		sldArms.value,
		sldHands.value,
		sldHead.value,
		sldLegs.value,
		sldFeet.value,
		sldMass.value,
		sldHips.value,
		sldFat.value))

		temparray = #()
		for i = 1 to numPresets do (
			append temparray presets[i][1]
		)
		ddlPresets.items = temparray
		
		update_root_scale()
	
	)

	on btnApply pressed do (
	
		sldShoulders.value = presets[ddlPresets.selection][2][1]
		sldHeight.value = presets[ddlPresets.selection][2][2]
		sldArms.value = presets[ddlPresets.selection][2][3]
		sldHands.value = presets[ddlPresets.selection][2][4]
		sldHead.value = presets[ddlPresets.selection][2][5]
		sldLegs.value = presets[ddlPresets.selection][2][6]
		sldFeet.value = presets[ddlPresets.selection][2][7]
		sldMass.value = presets[ddlPresets.selection][2][8]
		sldHips.value = presets[ddlPresets.selection][2][9]
		sldFat.value = presets[ddlPresets.selection][2][10]
	
	)
	
	on ddlPresets selected sel do(
	
		txtPresetName.text = presets[ddlPresets.selection][1]
	
	)
	
	on btnDelete pressed do(
	
		if numPresets > 1 then (
		
			temparray = deleteItem presets ddlPresets.selection
			presets = temparray
			numPresets = numPresets - 1
			
			temparray = #()
		
			for i = 1 to numPresets do (
			
				append temparray presets[i][1]
			
			)
			
			ddlPresets.items = temparray
			
			ddlPresets.selection = 1
			
			txtPresetName.text = presets[ddlPresets.selection][1]
			
			update_root_scale

		)

	
	)
	
	on cryptScaling open do (
	
		if (GetUserProp $Props_Scale "ScalePresets" == "") or (GetUserProp $Props_Scale "ScalePresets" == undefined) then SetUserProp $Props_Scale "ScalePresets" "Default,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0*"

		numPresets = 0	
		
		presetstmp = #()
		presets = #()
		
		preset_string_tmp = GetUserProp $Props_Scale "ScalePresets"
		preset_string = preset_string_tmp as stringStream
		
		while not eof preset_string do(
		
			append presetstmp (readDelimitedString preset_string "*")
			numPresets = numPresets + 1
		
		)
		
		for i = 1 to numPresets do(
		
			preset_stream = presetstmp[i] as stringStream
			
			append presets #(readDelimitedString preset_stream ",", #(
		
						readValue preset_stream,
						readValue preset_stream,
						readValue preset_stream,
						readValue preset_stream,
						readValue preset_stream,
						readValue preset_stream,
						readValue preset_stream,
						readValue preset_stream,
						readValue preset_stream,
						readValue preset_stream))
					
			)
		
		temparray = #()
		
		for i = 1 to numPresets do (
		
			append temparray presets[i][1]
		
		)
		
		ddlPresets.items = temparray
		
		txtPresetName.text = presets[ddlPresets.selection][1]
	
	)

)