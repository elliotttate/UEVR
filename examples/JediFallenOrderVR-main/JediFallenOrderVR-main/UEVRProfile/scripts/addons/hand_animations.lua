
local M = {}

local handPositions = {}

handPositions["right_trigger_weapon"] = {}
handPositions["right_trigger_weapon"]["on"] = {}
handPositions["right_trigger_weapon"]["on"]["RightHandIndex1_JNT"] = {13.954909324646, 19.658151626587, 12.959843635559}
handPositions["right_trigger_weapon"]["on"]["RightHandIndex2_JNT"] = {-7.2438044548035, 66.065002441406, -3.0500452518463}
handPositions["right_trigger_weapon"]["on"]["RightHandIndex3_JNT"] = {-4.330756187439, 11.854818344116, -4.8701190948486}
handPositions["right_trigger_weapon"]["off"] = {}
handPositions["right_trigger_weapon"]["off"]["RightHandIndex1_JNT"] = {13.954922676086, 14.658146858215, 12.959842681885}
handPositions["right_trigger_weapon"]["off"]["RightHandIndex2_JNT"] = {-7.2438387870789, 36.064968109131, -3.0500030517578}
handPositions["right_trigger_weapon"]["off"]["RightHandIndex3_JNT"] = {-4.330756187439, 11.854819297791, -4.8701119422913}

handPositions["right_grip"] = {}
handPositions["right_grip"]["on"] = {}
handPositions["right_grip"]["on"]["r_finThumbA"] = {18.892992019653, 21.963422775269, 98.08171081543}
handPositions["right_grip"]["on"]["r_finThumbB"] = {-7.7439656257629, 8.5948114395142, 1.4768767356873}
handPositions["right_grip"]["on"]["r_finThumbC"] = {0.45494520664215, 53.338161468506, 0.47714364528656}
handPositions["right_grip"]["on"]["r_finIndexA"] = {-0.91280007362366, 34.248249053955, -11.808694839478}
handPositions["right_grip"]["on"]["r_finIndexB"] = {0.26700574159622, 75.685409545898, 0.46499171853065}
handPositions["right_grip"]["on"]["r_finIndexC"] = {0.13736875355244, 40.267658233643, 0.097736105322838}
handPositions["right_grip"]["on"]["r_finMidA"] = {5.250946521759, 62.718563079834, -5.7843642234802}
handPositions["right_grip"]["on"]["r_finMidB"] = {6.8301887949929e-06, 63.31632232666, 4.378328412713e-06}
handPositions["right_grip"]["on"]["r_finMidC"] = {0.0, 51.839614868164, 2.868836872949e-06}
handPositions["right_grip"]["on"]["r_finRingA"] = {13.508856773376, 88.111824035645, -0.5625935792923}
handPositions["right_grip"]["on"]["r_finRingB"] = {-1.286438703537, 49.49654006958, -0.68736463785172}
handPositions["right_grip"]["on"]["r_finRingC"] = {-0.16671124100685, 52.429664611816, -0.19045147299767}
handPositions["right_grip"]["on"]["r_finPinkyA"] = {19.939207077026, 85.403076171875, 13.295107841492}
handPositions["right_grip"]["on"]["r_finPinkyB"] = {-6.8301887949929e-06, 34.366458892822, -1.9325034372741e-07}
handPositions["right_grip"]["on"]["r_finPinkyC"] = {-6.8301887949929e-06, 48.38236618042, 4.1190878619091e-06}



handPositions["right_grip"]["off"] = {}
--4handPositions["right_grip"]["off"]["RightHandThumb1_JNT"] = 	{-44.386493682861, 22.437026977539, -76.045600891113}
--4handPositions["right_grip"]["off"]["RightHandThumb2_JNT"] = 	{4.0847191810608, 18.195903778076, -11.097467422485}
--4handPositions["right_grip"]["off"]["RightHandThumb3_JNT"] = 	{0.0, 0.0, 0.0}
--4--handPositions["right_grip"]["off"]["RightHandIndex1_JNT"] = 	{-5.4112854003906, 10.378118515015, -0.9175192117691}
--4--handPositions["right_grip"]["off"]["RightHandIndex2_JNT"] = 	{-1.4336975812912, 23.672792434692, -0.97983050346375}
--4--handPositions["right_grip"]["off"]["RightHandIndex3_JNT"] = 	{0.0, -8.5377348568727e-07, 0.0}
--4handPositions["right_grip"]["off"]["RightHandMiddle1_JNT"] = 	{5.9782729148865, 2.1833770275116, -4.0905966758728}
--4handPositions["right_grip"]["off"]["RightHandMiddle2_JNT"] = 	{-28.41870880127, 74.714668273926, 27.525941848755}
--4handPositions["right_grip"]["off"]["RightHandMiddle3_JNT"] = 	{0.0, 3.3350531225551e-07, -1.5530051302887e-16}
--4handPositions["right_grip"]["off"]["RightHandRing1_JNT"] = 		{-3.3767223358154, 4.1980667114258, -7.3919062614441}
--4handPositions["right_grip"]["off"]["RightHandRing2_JNT"] = 		{-45.109657287598, 79.521903991699, 17.716226577759}
--4handPositions["right_grip"]["off"]["RightHandRing3_JNT"] = 		{0.0, 3.7352592130446e-07, 0.0}
--4handPositions["right_grip"]["off"]["RightHandPinky1_JNT"] = 	{-9.5717582702637, 3.7818260192871, -1.7375682592392}
--4handPositions["right_grip"]["off"]["RightHandPinky2_JNT"] = 	{-23.376274108887, 30.071979522705, 5.2131567001343}
--4handPositions["right_grip"]["off"]["RightHandPinky3_JNT"] = 	{0.0, 1.7075471987482e-06, -2.544443605465e-14}

--handPositions["left_grip"]["off"]["index_01_l"] 	= {-5.289530707188e-15, -23.372999646514, 1.9060797867025e-15}
--handPositions["left_grip"]["off"]["index_02_l"] 	= {8.0658609505893e-15, -14.892568419111, 4.9600607677067e-15}
--handPositions["left_grip"]["off"]["index_03_l"] 	= {5.4564280030652e-15, -12.516400997547, -8.5974214905172e-15}
--handPositions["left_grip"]["off"]["middle_01_l"]	 	= {8.4606676420082e-15, -31.572682017398, 1.4297090224516e-15}
--handPositions["left_grip"]["off"]["middle_02_l"] 	= {8.1856491538175e-15, -20.76921047774, 4.9669654892835e-15}
--handPositions["left_grip"]["off"]["middle_03_l"] 	= {-6.0596998166608e-15, -9.9999999709534, 3.7228589203538e-15}
--handPositions["left_grip"]["off"]["thumb_01_l"] 	= {-39.904178427023, -20.508675504417, 73.56446390775}
--handPositions["left_grip"]["off"]["thumb_02_l"] 	= {1.9322904957701, -23.246005781061, 3.5306280002014}
--handPositions["left_grip"]["off"]["thumb_03_l"] 	= {-1.0139117314112e-14, -9.9999999709534, 7.2724655877018e-15}
--handPositions["left_grip"]["off"]["pinky_01_l"] 	= {-0.60504264270136, -14.833680882859, 10.491640062438}
--handPositions["left_grip"]["off"]["pinky_02_l"] 	= {-5.8744066303758e-16, -21.286999049244, -3.1258345364381e-15}
--handPositions["left_grip"]["off"]["pinky_03_l"]	 = {-3.1776271511263e-15, -4.9170000470224, 1.3643229164878e-16}
--handPositions["left_grip"]["off"]["ring_01_l"] 	= {0.11693801365859, -29.414482479749, 6.3958444445851}
--handPositions["left_grip"]["off"]["ring_02_l"] 	= {7.8427501255157e-15, -18.963999541972, -1.3098934994284e-15}
--handPositions["left_grip"]["off"]["ring_03_l"] 	= {1.0768069584223e-15, -9.167999748025, -6.4678575372264e-15}
handPositions["right_grip"]["off"]["r_finThumbA"] = {18.892992019653, 21.963422775269, 98.08171081543}
handPositions["right_grip"]["off"]["r_finThumbB"] = {-7.7439656257629, 8.5948114395142, 1.4768767356873}
handPositions["right_grip"]["off"]["r_finThumbC"] = {0.45494520664215, 53.338161468506, 0.47714364528656}
handPositions["right_grip"]["off"]["r_finIndexA"] = {-0.91280007362366, 34.248249053955, -11.808694839478}
handPositions["right_grip"]["off"]["r_finIndexB"] = {0.26700574159622, 75.685409545898, 0.46499171853065}
handPositions["right_grip"]["off"]["r_finIndexC"] = {0.13736875355244, 40.267658233643, 0.097736105322838}
handPositions["right_grip"]["off"]["r_finMidA"] = {5.250946521759, 62.718563079834, -5.7843642234802}
handPositions["right_grip"]["off"]["r_finMidB"] = {6.8301887949929e-06, 63.31632232666, 4.378328412713e-06}
handPositions["right_grip"]["off"]["r_finMidC"] = {0.0, 51.839614868164, 2.868836872949e-06}
handPositions["right_grip"]["off"]["r_finRingA"] = {13.508856773376, 88.111824035645, -0.5625935792923}
handPositions["right_grip"]["off"]["r_finRingB"] = {-1.286438703537, 49.49654006958, -0.68736463785172}
handPositions["right_grip"]["off"]["r_finRingC"] = {-0.16671124100685, 52.429664611816, -0.19045147299767}
handPositions["right_grip"]["off"]["r_finPinkyA"] = {19.939207077026, 85.403076171875, 13.295107841492}
handPositions["right_grip"]["off"]["r_finPinkyB"] = {-6.8301887949929e-06, 34.366458892822, -1.9325034372741e-07}
handPositions["right_grip"]["off"]["r_finPinkyC"] = {-6.8301887949929e-06, 48.38236618042, 4.1190878619091e-06}




handPositions["right_grip_weapon"] = {}
handPositions["right_grip_weapon"]["on"] = {}
handPositions["right_grip_weapon"]["on"]["r_finThumbA"] = {18.892992019653, 21.963422775269, 98.08171081543}
handPositions["right_grip_weapon"]["on"]["r_finThumbB"] = {-7.7439656257629, 8.5948114395142, 1.4768767356873}
handPositions["right_grip_weapon"]["on"]["r_finThumbC"] = {0.45494520664215, 53.338161468506, 0.47714364528656}
handPositions["right_grip_weapon"]["on"]["r_finIndexA"] = {-0.91280007362366, 34.248249053955, -11.808694839478}
handPositions["right_grip_weapon"]["on"]["r_finIndexB"] = {0.26700574159622, 75.685409545898, 0.46499171853065}
handPositions["right_grip_weapon"]["on"]["r_finIndexC"] = {0.13736875355244, 40.267658233643, 0.097736105322838}
handPositions["right_grip_weapon"]["on"]["r_finMidA"] = {5.250946521759, 62.718563079834, -5.7843642234802}
handPositions["right_grip_weapon"]["on"]["r_finMidB"] = {6.8301887949929e-06, 63.31632232666, 4.378328412713e-06}
handPositions["right_grip_weapon"]["on"]["r_finMidC"] = {0.0, 51.839614868164, 2.868836872949e-06}
handPositions["right_grip_weapon"]["on"]["r_finRingA"] = {13.508856773376, 88.111824035645, -0.5625935792923}
handPositions["right_grip_weapon"]["on"]["r_finRingB"] = {-1.286438703537, 49.49654006958, -0.68736463785172}
handPositions["right_grip_weapon"]["on"]["r_finRingC"] = {-0.16671124100685, 52.429664611816, -0.19045147299767}
handPositions["right_grip_weapon"]["on"]["r_finPinkyA"] = {19.939207077026, 85.403076171875, 13.295107841492}
handPositions["right_grip_weapon"]["on"]["r_finPinkyB"] = {-6.8301887949929e-06, 34.366458892822, -1.9325034372741e-07}
handPositions["right_grip_weapon"]["on"]["r_finPinkyC"] = {-6.8301887949929e-06, 48.38236618042, 4.1190878619091e-06}


handPositions["right_trigger"] = {}
handPositions["right_trigger"]["on"] = {}
handPositions["right_trigger"]["on"]["RightHandIndex1_JNT"] = {3.9550070762634, 64.658241271973, 7.9599676132202}
handPositions["right_trigger"]["on"]["RightHandIndex2_JNT"] = {-7.2439274787903, 106.06492614746, -3.0500290393829}
handPositions["right_trigger"]["on"]["RightHandIndex3_JNT"] = {5.6692137718201, 31.855098724365, -34.870063781738}
handPositions["right_trigger"]["off"] = {}
handPositions["right_trigger"]["off"]["RightHandIndex1_JNT"] = {-5.4112854003906, 10.378118515015, -0.9175192117691}
handPositions["right_trigger"]["off"]["RightHandIndex2_JNT"] = {-1.4336975812912, 23.672792434692, -0.97983050346375}
handPositions["right_trigger"]["off"]["RightHandIndex3_JNT"] = {0.0, -8.5377348568727e-07, 0.0}

handPositions["right_thumb"] = {}
handPositions["right_thumb"]["on"] = {}
handPositions["right_thumb"]["on"]["RightHandThumb1_JNT"] = {-36.203105926514, 44.766750335693, -85.575386047363}
handPositions["right_thumb"]["on"]["RightHandThumb2_JNT"] = {11.012429237366, 42.29390335083, -40.077499389648}
handPositions["right_thumb"]["on"]["RightHandThumb3_JNT"] = {-7.0979390144348, 43.205421447754, -4.4764862060547}
handPositions["right_thumb"]["off"] = {}
handPositions["right_thumb"]["off"]["RightHandThumb1_JNT"] = {-44.386493682861, 22.437026977539, -76.045600891113}
handPositions["right_thumb"]["off"]["RightHandThumb2_JNT"] = {4.0847191810608, 18.195903778076, -11.097467422485}
handPositions["right_thumb"]["off"]["RightHandThumb3_JNT"] = {0.0, 0.0, 0.0}


-- handPositions["right_trigger"] = {}
-- handPositions["right_trigger"]["off"] = {}
-- handPositions["right_trigger"]["off"]["RightHandIndex1_JNT"] = {-5.4112854003906, 10.378118515015, -0.9175192117691}
-- handPositions["right_trigger"]["off"]["RightHandIndex2_JNT"] = {-1.4336975812912, 23.672792434692, -0.97983050346375}
-- handPositions["right_trigger"]["off"]["RightHandIndex3_JNT"] = {0.0, -8.5377348568727e-07, 0.0}


-- handPositions["right_thumb"] = {}
-- handPositions["right_thumb"]["off"] = {}
-- handPositions["right_thumb"]["off"]["RightHandThumb1_JNT"] = {-44.386493682861, 22.437026977539, -76.045600891113}
-- handPositions["right_thumb"]["off"]["RightHandThumb2_JNT"] = {4.0847191810608, 18.195903778076, -11.097467422485}
-- handPositions["right_thumb"]["off"]["RightHandThumb3_JNT"] = {0.0, 0.0, 0.0}


handPositions["left_trigger"] = {}
handPositions["left_trigger"]["on"] = {}
handPositions["left_trigger"]["l_finThumbA"] = {16.595794677734, 39.08504486084, 128.27113342285}
handPositions["left_trigger"]["l_finThumbB"] = {-8.8665409088135, 18.739213943481, -0.043349072337151}
handPositions["left_trigger"]["l_finThumbC"] = {0.46754691004753, 5.245276927948, -0.027901779860258}
handPositions["left_trigger"]["l_finIndexA"] = {2.2802312374115, 16.690685272217, -6.3316149711609}
handPositions["left_trigger"]["l_finIndexB"] = {-0.012997848913074, 26.87742805481, 0.84193432331085}
handPositions["left_trigger"]["l_finIndexC"] = {0.12118121236563, 10.184586524963, 0.00076547142816707}
handPositions["left_trigger"]["l_finMidA"] = {1.7745581865311, 18.368068695068, -1.5560227632523}
handPositions["left_trigger"]["l_finMidB"] = {0.092617362737656, 27.769115447998, -0.13504821062088}
handPositions["left_trigger"]["l_finMidC"] = {0.3677237033844, 8.484001159668, -0.79065173864365}
handPositions["left_trigger"]["l_finRingA"] = {-1.779523730278, 29.635919570923, -10.543623924255}
handPositions["left_trigger"]["l_finRingB"] = {1.366645693779, 42.642868041992, -0.74509185552597}
handPositions["left_trigger"]["l_finRingC"] = {0.15417784452438, 12.1068983078, -0.81846779584885}
handPositions["left_trigger"]["l_finPinkyA"] = {3.7041206359863, 19.115125656128, -13.841408729553}
handPositions["left_trigger"]["l_finPinkyB"] = {0.70486867427826, 34.974048614502, 0.27033406496048}
handPositions["left_trigger"]["l_finPinkyC"] = {0.0, 14.318551063538, -6.7769369707094e-06}








handPositions["left_trigger"]["off"] = {}
-- handPositions["left_trigger"]["off"]["LeftHandIndex1_JNT"] = {-7.6476874351501, 18.381666183472, 3.1531648635864}
-- handPositions["left_trigger"]["off"]["LeftHandIndex2_JNT"] = {5.5328216552734, 10.331413269043, 4.1289820671082}
-- handPositions["left_trigger"]["off"]["LeftHandIndex3_JNT"] = {4.4370613098145, -3.4452188014984, 2.7750282287598}
handPositions["left_trigger"]["off"]["LeftHandIndex1_JNT"] = {5.4113330841064, 10.378183364868, 0.91737693548203}
handPositions["left_trigger"]["off"]["LeftHandIndex2_JNT"] = {1.4339435100555, 23.6731300354, 0.97924590110779}
handPositions["left_trigger"]["off"]["LeftHandIndex3_JNT"] = {0.0, 0.0, 0.0}


handPositions["left_grip"] = {}
handPositions["left_grip"]["on"] = {}
handPositions["left_grip"]["l_finThumbA"] = {16.595794677734, 39.08504486084, 128.27113342285}
handPositions["left_grip"]["l_finThumbB"] = {-8.8665409088135, 18.739213943481, -0.043349072337151}
handPositions["left_grip"]["l_finThumbC"] = {0.46754691004753, 5.245276927948, -0.027901779860258}
handPositions["left_grip"]["l_finIndexA"] = {2.2802312374115, 16.690685272217, -6.3316149711609}
handPositions["left_grip"]["l_finIndexB"] = {-0.012997848913074, 26.87742805481, 0.84193432331085}
handPositions["left_grip"]["l_finIndexC"] = {0.12118121236563, 10.184586524963, 0.00076547142816707}
handPositions["left_grip"]["l_finMidA"] = {1.7745581865311, 18.368068695068, -1.5560227632523}
handPositions["left_grip"]["l_finMidB"] = {0.092617362737656, 27.769115447998, -0.13504821062088}
handPositions["left_grip"]["l_finMidC"] = {0.3677237033844, 8.484001159668, -0.79065173864365}
handPositions["left_grip"]["l_finRingA"] = {-1.779523730278, 29.635919570923, -10.543623924255}
handPositions["left_grip"]["l_finRingB"] = {1.366645693779, 42.642868041992, -0.74509185552597}
handPositions["left_grip"]["l_finRingC"] = {0.15417784452438, 12.1068983078, -0.81846779584885}
handPositions["left_grip"]["l_finPinkyA"] = {3.7041206359863, 19.115125656128, -13.841408729553}
handPositions["left_grip"]["l_finPinkyB"] = {0.70486867427826, 34.974048614502, 0.27033406496048}
handPositions["left_grip"]["l_finPinkyC"] = {0.0, 14.318551063538, -6.7769369707094e-06}
handPositions["left_grip_weapon"] = {}
handPositions["left_grip_weapon"]["on"] = {}
handPositions["left_grip_weapon"]["l_finThumbA"] = {16.595794677734, 39.08504486084, 128.27113342285}
handPositions["left_grip_weapon"]["l_finThumbB"] = {-8.8665409088135, 18.739213943481, -0.043349072337151}
handPositions["left_grip_weapon"]["l_finThumbC"] = {0.46754691004753, 5.245276927948, -0.027901779860258}
handPositions["left_grip_weapon"]["l_finIndexA"] = {2.2802312374115, 16.690685272217, -6.3316149711609}
handPositions["left_grip_weapon"]["l_finIndexB"] = {-0.012997848913074, 26.87742805481, 0.84193432331085}
handPositions["left_grip_weapon"]["l_finIndexC"] = {0.12118121236563, 10.184586524963, 0.00076547142816707}
handPositions["left_grip_weapon"]["l_finMidA"] = {1.7745581865311, 18.368068695068, -1.5560227632523}
handPositions["left_grip_weapon"]["l_finMidB"] = {0.092617362737656, 27.769115447998, -0.13504821062088}
handPositions["left_grip_weapon"]["l_finMidC"] = {0.3677237033844, 8.484001159668, -0.79065173864365}
handPositions["left_grip_weapon"]["l_finRingA"] = {-1.779523730278, 29.635919570923, -10.543623924255}
handPositions["left_grip_weapon"]["l_finRingB"] = {1.366645693779, 42.642868041992, -0.74509185552597}
handPositions["left_grip_weapon"]["l_finRingC"] = {0.15417784452438, 12.1068983078, -0.81846779584885}
handPositions["left_grip_weapon"]["l_finPinkyA"] = {3.7041206359863, 19.115125656128, -13.841408729553}
handPositions["left_grip_weapon"]["l_finPinkyB"] = {0.70486867427826, 34.974048614502, 0.27033406496048}
handPositions["left_grip_weapon"]["l_finPinkyC"] = {0.0, 14.318551063538, -6.7769369707094e-06}
--MIDDLEFINGER:
--["index_01_l"] = {-5.0, -68.372999646514, 1.6841512986224e-13}
--["index_02_l"] = {6.901257349627e-14, -74.892568419111, 2.8366169778173e-13}
--["index_03_l"] = {2.6479352317544e-13, -22.516400997547, 3.3644482884161e-13}
--["middle_01_l"] = {5.488301740553e-15, -31.572682017398, -9.814664794423e-15}
--["middle_02_l"] = {-1.0036532879688e-14, -20.76921047774, -6.2445387647544e-15}
--["middle_03_l"] = {2.8120368131574e-14, -9.9999999709534, -3.4387252254242e-14}
--["thumb_01_l"] = {-29.904178427024, -40.508675504416, 73.564463907749}
--["thumb_02_l"] = {6.9322904957701, -48.246005781061, 3.5306280002008}
--["thumb_03_l"] = {5.0, -44.999999970954, 5.1342669328857e-13}
--["pinky_01_l"] = {4.3949573572993, -64.833680882859, 10.491640062439}
--["pinky_02_l"] = {-7.2618449521822e-14, -61.286999049244, 6.1504840494547e-14}
--["pinky_03_l"] = {-10.0, -54.917000047023, 2.1719193586179e-13}
--["ring_01_l"] = {0.11693801365759, -79.414482479749, 6.3958444445847}
--["ring_02_l"] = {-9.9999999999998, -63.963999541972, 9.1963425630699e-13}
--["ring_03_l"] = {8.5685062034591e-14, -34.167999748026, -3.0583345954234e-13}
handPositions["left_grip"]["off"] = {}
handPositions["left_grip"]["l_finThumbA"] = {16.595794677734, 39.08504486084, 128.27113342285}
handPositions["left_grip"]["l_finThumbB"] = {-8.8665409088135, 18.739213943481, -0.043349072337151}
handPositions["left_grip"]["l_finThumbC"] = {0.46754691004753, 5.245276927948, -0.027901779860258}
handPositions["left_grip"]["l_finIndexA"] = {2.2802312374115, 16.690685272217, -6.3316149711609}
handPositions["left_grip"]["l_finIndexB"] = {-0.012997848913074, 26.87742805481, 0.84193432331085}
handPositions["left_grip"]["l_finIndexC"] = {0.12118121236563, 10.184586524963, 0.00076547142816707}
handPositions["left_grip"]["l_finMidA"] = {1.7745581865311, 18.368068695068, -1.5560227632523}
handPositions["left_grip"]["l_finMidB"] = {0.092617362737656, 27.769115447998, -0.13504821062088}
handPositions["left_grip"]["l_finMidC"] = {0.3677237033844, 8.484001159668, -0.79065173864365}
handPositions["left_grip"]["l_finRingA"] = {-1.779523730278, 29.635919570923, -10.543623924255}
handPositions["left_grip"]["l_finRingB"] = {1.366645693779, 42.642868041992, -0.74509185552597}
handPositions["left_grip"]["l_finRingC"] = {0.15417784452438, 12.1068983078, -0.81846779584885}
handPositions["left_grip"]["l_finPinkyA"] = {3.7041206359863, 19.115125656128, -13.841408729553}
handPositions["left_grip"]["l_finPinkyB"] = {0.70486867427826, 34.974048614502, 0.27033406496048}
handPositions["left_grip"]["l_finPinkyC"] = {0.0, 14.318551063538, -6.7769369707094e-06}
-- handPositions["left_grip"]["off"]["LeftHandThumb1_JNT"] = {34.340190887451, 14.38916015625, 65.486839294434}
-- handPositions["left_grip"]["off"]["LeftHandThumb2_JNT"] = {-10.788597106934, 13.845867156982, 13.92795753479}
-- handPositions["left_grip"]["off"]["LeftHandThumb3_JNT"] = {-1.5474680662155, -4.5875201225281, 0.33849230408669}
-- -- ["LeftHandIndex1_JNT"] = {-7.6477212905884, 33.381671905518, 3.1532111167908}
-- -- ["LeftHandIndex2_JNT"] = {5.5326647758484, 40.331390380859, 4.12921667099}
-- -- ["LeftHandIndex3_JNT"] = {4.4370546340942, 16.55474281311, 2.774961233139}
-- handPositions["left_grip"]["off"]["LeftHandMiddle1_JNT"] = {-3.1590783596039, 24.584951400757, 6.9041600227356}
-- handPositions["left_grip"]["off"]["LeftHandMiddle2_JNT"] = {30.271335601807, 95.231437683105, -20.437683105469}
-- handPositions["left_grip"]["off"]["LeftHandMiddle3_JNT"] = {-6.853759765625, 21.327611923218, -5.0963525772095}
-- handPositions["left_grip"]["off"]["LeftHandRing1_JNT"] = {18.182447433472, 20.185804367065, 17.630153656006}
-- handPositions["left_grip"]["off"]["LeftHandRing2_JNT"] = {47.592178344727, 102.84102630615, -4.5726056098938}
-- handPositions["left_grip"]["off"]["LeftHandRing3_JNT"] = {-0.25971794128418, 26.115886688232, -5.0124006271362}
-- handPositions["left_grip"]["off"]["LeftHandPinky1_JNT"] = {23.526824951172, 6.1054487228394, 5.9132800102234}
-- handPositions["left_grip"]["off"]["LeftHandPinky2_JNT"] = {37.618530273438, 62.690238952637, 5.9132323265076}
-- handPositions["left_grip"]["off"]["LeftHandPinky3_JNT"] = {14.951310157776, 27.93567276001, -1.4868113994598}


handPositions["left_thumb"] = {}
handPositions["left_thumb"]["on"] = {}
handPositions["left_thumb"]["on"]["LeftHandThumb1_JNT"] = {29.340223312378, 14.388812065125, 65.486557006836}
handPositions["left_thumb"]["on"]["LeftHandThumb2_JNT"] = {-10.78872013092, 33.845855712891, 13.92786693573}
handPositions["left_thumb"]["on"]["LeftHandThumb3_JNT"] = {-1.547474861145, -4.5875172615051, 0.33850952982903}
handPositions["left_thumb"]["off"] = {}
handPositions["left_thumb"]["off"]["LeftHandThumb1_JNT"] = {34.340190887451, 14.38916015625, 65.486839294434}
handPositions["left_thumb"]["off"]["LeftHandThumb2_JNT"] = {-10.788597106934, 13.845867156982, 13.92795753479}
handPositions["left_thumb"]["off"]["LeftHandThumb3_JNT"] = {-1.5474680662155, -4.5875201225281, 0.33849230408669}

local poses = {}
poses["open_left"] = { {"left_grip","off"}, {"left_trigger","off"}, {"left_thumb","off"} }
poses["open_right"] = { {"right_grip","off"}, {"right_trigger","off"}, {"right_thumb","off"} }
poses["grip_right_weapon"] = { {"right_grip_weapon","on"}, {"right_trigger_weapon","off"} }
poses["grip_left_weapon"] = {{"left_grip_weapon","on"}}

M.positions = handPositions
M.poses = poses

return M



--[[
["LeftHandThumb1_JNT"] = {44.386615753174, 22.437032699585, 76.045318603516}
["LeftHandThumb2_JNT"] = {-4.0847191810608, 18.195880889893, 11.097414016724}
["LeftHandThumb3_JNT"] = {0.0, 0.0, 0.0}
["LeftHandIndex1_JNT"] = {5.4113330841064, 10.378183364868, 0.91737693548203}
["LeftHandIndex2_JNT"] = {1.4339435100555, 23.6731300354, 0.97924590110779}
["LeftHandIndex3_JNT"] = {0.0, 0.0, 0.0}
["LeftHandMiddle1_JNT"] = {-5.9782662391663, 2.1833848953247, 4.0906004905701}
["LeftHandMiddle2_JNT"] = {28.419492721558, 74.716064453125, -27.526908874512}
["LeftHandMiddle3_JNT"] = {0.0, 0.0, 0.0}
["LeftHandRing1_JNT"] = {3.376749753952, 4.1980848312378, 7.3918490409851}
["LeftHandRing2_JNT"] = {45.110187530518, 79.523498535156, -17.716432571411}
["LeftHandRing3_JNT"] = {0.0, 0.0, 0.0}
["LeftHandPinky1_JNT"] = {9.5718126296997, 3.7818431854248, 1.7374849319458}
["LeftHandPinky2_JNT"] = {23.376853942871, 30.072662353516, -5.2138328552246}
["LeftHandPinky3_JNT"] = {0.0, 0.0, 0.0}
["RightHandThumb1_JNT"] = {-44.386493682861, 22.437026977539, -76.045600891113}
["RightHandThumb2_JNT"] = {4.0847191810608, 18.195903778076, -11.097467422485}
["RightHandThumb3_JNT"] = {0.0, 0.0, 0.0}
["RightHandIndex1_JNT"] = {-5.4112854003906, 10.378118515015, -0.9175192117691}
["RightHandIndex2_JNT"] = {-1.4336975812912, 23.672792434692, -0.97983050346375}
["RightHandIndex3_JNT"] = {0.0, -8.5377348568727e-07, 0.0}
["RightHandMiddle1_JNT"] = {5.9782729148865, 2.1833770275116, -4.0905966758728}
["RightHandMiddle2_JNT"] = {-28.41870880127, 74.714668273926, 27.525941848755}
["RightHandMiddle3_JNT"] = {0.0, 3.3350531225551e-07, -1.5530051302887e-16}
["RightHandRing1_JNT"] = {-3.3767223358154, 4.1980667114258, -7.3919062614441}
["RightHandRing2_JNT"] = {-45.109657287598, 79.521903991699, 17.716226577759}
["RightHandRing3_JNT"] = {0.0, 3.7352592130446e-07, 0.0}
["RightHandPinky1_JNT"] = {-9.5717582702637, 3.7818260192871, -1.7375682592392}
["RightHandPinky2_JNT"] = {-23.376274108887, 30.071979522705, 5.2131567001343}
["RightHandPinky3_JNT"] = {0.0, 1.7075471987482e-06, -2.544443605465e-14}


["LeftHandThumb1_JNT"] = {29.340223312378, 14.388812065125, 65.486557006836}
["LeftHandThumb2_JNT"] = {-10.78872013092, 33.845855712891, 13.92786693573}
["LeftHandThumb3_JNT"] = {-1.547474861145, -4.5875172615051, 0.33850952982903}
["LeftHandIndex1_JNT"] = {-7.6477212905884, 33.381671905518, 3.1532111167908}
["LeftHandIndex2_JNT"] = {5.5326647758484, 40.331390380859, 4.12921667099}
["LeftHandIndex3_JNT"] = {4.4370546340942, 16.55474281311, 2.774961233139}
["LeftHandMiddle1_JNT"] = {-3.1590783596039, 24.584951400757, 6.9041600227356}
["LeftHandMiddle2_JNT"] = {30.271335601807, 95.231437683105, -20.437683105469}
["LeftHandMiddle3_JNT"] = {-6.853759765625, 21.327611923218, -5.0963525772095}
["LeftHandRing1_JNT"] = {18.182447433472, 20.185804367065, 17.630153656006}
["LeftHandRing2_JNT"] = {47.592178344727, 102.84102630615, -4.5726056098938}
["LeftHandRing3_JNT"] = {-0.25971794128418, 26.115886688232, -5.0124006271362}
["LeftHandPinky1_JNT"] = {23.526824951172, 6.1054487228394, 5.9132800102234}
["LeftHandPinky2_JNT"] = {37.618530273438, 62.690238952637, 5.9132323265076}
["LeftHandPinky3_JNT"] = {14.951310157776, 27.93567276001, -1.4868113994598}
["RightHandThumb1_JNT"] = {-36.203144073486, 34.766864776611, -85.575454711914}
["RightHandThumb2_JNT"] = {11.012476921082, 42.293972015381, -40.077541351318}
["RightHandThumb3_JNT"] = {-7.0980415344238, 8.2054109573364, -4.4765815734863}
["RightHandIndex1_JNT"] = {13.954929351807, 14.658153533936, 12.959844589233}
["RightHandIndex2_JNT"] = {-7.2438387870789, 36.064979553223, -3.0500068664551}
["RightHandIndex3_JNT"] = {-4.330756187439, 11.854824066162, -4.8701167106628}
["RightHandMiddle1_JNT"] = {3.2178385257721, 27.142356872559, 2.3389554023743}
["RightHandMiddle2_JNT"] = {-27.03067779541, 137.21624755859, -2.6563432216644}
["RightHandMiddle3_JNT"] = {9.8199949264526, 49.10147857666, 6.0561528205872}
["RightHandRing1_JNT"] = {-18.489757537842, 24.656513214111, 2.7107570171356}
["RightHandRing2_JNT"] = {-37.691459655762, 128.56390380859, -15.069981575012}
["RightHandRing3_JNT"] = {5.6724443435669, 63.004455566406, 5.0134029388428}
["RightHandPinky1_JNT"] = {-30.595754623413, 12.251677513123, 1.6611989736557}
["RightHandPinky2_JNT"] = {-58.668590545654, 75.370979309082, -16.722003936768}
["RightHandPinky3_JNT"] = {-21.26050567627, 28.24077796936, 20.569374084473}

]]--
