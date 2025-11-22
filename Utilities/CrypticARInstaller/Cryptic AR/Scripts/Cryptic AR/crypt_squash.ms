global rarm
global larm
global rleg
global lleg
global torso
global head

rollout cryptSquash "Cryptic AR - Squash and Stretch" width:307 height:224
(
	spinner spnRArm "R_Arm Stretch" pos:[51,122] width:85 height:16 range:[-100,100,0] controller:rarm
	spinner spnLArm "L_Arm Stretch" pos:[205,122] width:85 height:16 range:[-100,100,0] controller:larm
	spinner spnRLeg "R_Leg Stretch" pos:[51,195] width:85 height:16 range:[-100,100,0] controller:rleg
	spinner spnLLeg "L_Leg Stretch" pos:[205,195] width:85 height:16 range:[-100,100,0] controller:lleg
	spinner spnHead "Head Stretch" pos:[51,50] width:85 height:16 range:[-100,100,0] controller:head
	spinner spnTorso "Torso Stretch" pos:[205,50] width:85 height:16 range:[-100,100,0] controller:torso
	button btnTorso "Torso" pos:[183,11] width:96 height:32
	button btnLArm "L_Arm" pos:[183,83] width:96 height:32
	button btnRArm "R_Arm" pos:[29,83] width:96 height:32
	button btnHead "Head" pos:[29,11] width:96 height:32
	button btnRLeg "R_Leg" pos:[29,155] width:96 height:32
	button btnLLeg "L_Leg" pos:[183,155] width:96 height:32

	on btnTorso pressed do(
	
		if keyboard.controlPressed == false then clearselection()
		selectMore $Center_root
	
	)
	
	on btnLArm pressed do(
	
		if keyboard.controlPressed == false then clearselection()
		selectMore $IK_HandL
	
	)
	
	on btnRArm pressed do(
	
		if keyboard.controlPressed == false then clearselection()
		selectMore $IK_HandR
	
	)
	
	on btnHead pressed do(
	
		if keyboard.controlPressed == false then clearselection()
		selectMore $Head
	
	)
	
	on btnRLeg pressed do(
	
		if keyboard.controlPressed == false then clearselection()
		selectMore $IK_Control_FootR
	
	)
	
	on btnLLeg pressed do(
	
		if keyboard.controlPressed == false then clearselection()
		selectMore $IK_Control_FootL
	
	)
	
	on cryptSquash close do(

		callbacks.removeScripts #filePostOpen

	)	
	on cryptSquash open do(
	
		rarm = $IK_HandR.baseObject.Custom_Attributes.RArmStretch.Controller
		larm = $IK_HandL.baseObject.Custom_Attributes.LArmStretch.Controller
		rleg = $IK_Control_FootR.baseObject.Custom_Attributes.RLegStretch.Controller
		lleg = $IK_Control_FootL.baseObject.Custom_Attributes.LLegStretch.Controller
		torso = $Center_root.baseObject.Custom_Attributes.TorsoStretch.Controller
		head = $FK_Head.baseObject.Custom_Attributes.HeadStretch.Controller

	
	)
)