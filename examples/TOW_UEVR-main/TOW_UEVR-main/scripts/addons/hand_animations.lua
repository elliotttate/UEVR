
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
handPositions["right_grip"]["on"]["RightHandThumb1_JNT"] = {-36.203105926514, 44.766750335693, -85.575386047363}
handPositions["right_grip"]["on"]["RightHandThumb2_JNT"] = {11.012429237366, 42.29390335083, -40.077499389648}
handPositions["right_grip"]["on"]["RightHandThumb3_JNT"] = {-7.0979390144348, 43.205421447754, -4.4764862060547}
-- handPositions["right_grip"]["on"]["RightHandIndex1_JNT"] = {3.9550070762634, 64.658241271973, 7.9599676132202}
-- handPositions["right_grip"]["on"]["RightHandIndex2_JNT"] = {-7.2439274787903, 106.06492614746, -3.0500290393829}
-- handPositions["right_grip"]["on"]["RightHandIndex3_JNT"] = {5.6692137718201, 31.855098724365, -34.870063781738}
handPositions["right_grip"]["on"]["RightHandMiddle1_JNT"] = {-1.7821055650711, 57.142044067383, 17.339141845703}
handPositions["right_grip"]["on"]["RightHandMiddle2_JNT"] = {-7.030668258667, 142.21611022949, 12.343898773193}
handPositions["right_grip"]["on"]["RightHandMiddle3_JNT"] = {9.8199949264526, 49.101493835449, 6.0561537742615}
handPositions["right_grip"]["on"]["RightHandRing1_JNT"] = {-33.489711761475, 49.65669631958, -12.289658546448}
handPositions["right_grip"]["on"]["RightHandRing2_JNT"] = {-27.691537857056, 143.56373596191, -25.069917678833}
handPositions["right_grip"]["on"]["RightHandRing3_JNT"] = {5.6724443435669, 63.004455566406, 5.0134048461914}
handPositions["right_grip"]["on"]["RightHandPinky1_JNT"] = {-40.595561981201, 37.251636505127, -8.3391942977905}
handPositions["right_grip"]["on"]["RightHandPinky2_JNT"] = {-53.668056488037, 140.37017822266, -86.721374511719}
handPositions["right_grip"]["on"]["RightHandPinky3_JNT"] = {-21.260492324829, 28.240776062012, 20.569372177124}
handPositions["right_grip"]["off"] = {}
handPositions["right_grip"]["off"]["RightHandThumb1_JNT"] = {-44.386493682861, 22.437026977539, -76.045600891113}
handPositions["right_grip"]["off"]["RightHandThumb2_JNT"] = {4.0847191810608, 18.195903778076, -11.097467422485}
handPositions["right_grip"]["off"]["RightHandThumb3_JNT"] = {0.0, 0.0, 0.0}
--handPositions["right_grip"]["off"]["RightHandIndex1_JNT"] = {-5.4112854003906, 10.378118515015, -0.9175192117691}
--handPositions["right_grip"]["off"]["RightHandIndex2_JNT"] = {-1.4336975812912, 23.672792434692, -0.97983050346375}
--handPositions["right_grip"]["off"]["RightHandIndex3_JNT"] = {0.0, -8.5377348568727e-07, 0.0}
handPositions["right_grip"]["off"]["RightHandMiddle1_JNT"] = {5.9782729148865, 2.1833770275116, -4.0905966758728}
handPositions["right_grip"]["off"]["RightHandMiddle2_JNT"] = {-28.41870880127, 74.714668273926, 27.525941848755}
handPositions["right_grip"]["off"]["RightHandMiddle3_JNT"] = {0.0, 3.3350531225551e-07, -1.5530051302887e-16}
handPositions["right_grip"]["off"]["RightHandRing1_JNT"] = {-3.3767223358154, 4.1980667114258, -7.3919062614441}
handPositions["right_grip"]["off"]["RightHandRing2_JNT"] = {-45.109657287598, 79.521903991699, 17.716226577759}
handPositions["right_grip"]["off"]["RightHandRing3_JNT"] = {0.0, 3.7352592130446e-07, 0.0}
handPositions["right_grip"]["off"]["RightHandPinky1_JNT"] = {-9.5717582702637, 3.7818260192871, -1.7375682592392}
handPositions["right_grip"]["off"]["RightHandPinky2_JNT"] = {-23.376274108887, 30.071979522705, 5.2131567001343}
handPositions["right_grip"]["off"]["RightHandPinky3_JNT"] = {0.0, 1.7075471987482e-06, -2.544443605465e-14}


handPositions["right_grip_weapon"] = {}
handPositions["right_grip_weapon"]["on"] = {}
handPositions["right_grip_weapon"]["on"]["RightHandThumb1_JNT"] = {-36.203144073486, 34.766864776611, -85.575454711914}
handPositions["right_grip_weapon"]["on"]["RightHandThumb2_JNT"] = {11.012476921082, 42.293972015381, -40.077541351318}
handPositions["right_grip_weapon"]["on"]["RightHandThumb3_JNT"] = {-7.0980415344238, 8.2054109573364, -4.4765815734863}
-- handPositions["right_grip_weapon"]["on"]["RightHandIndex1_JNT"] = {13.954929351807, 14.658153533936, 12.959844589233}
-- handPositions["right_grip_weapon"]["on"]["RightHandIndex2_JNT"] = {-7.2438387870789, 36.064979553223, -3.0500068664551}
-- handPositions["right_grip_weapon"]["on"]["RightHandIndex3_JNT"] = {-4.330756187439, 11.854824066162, -4.8701167106628}
handPositions["right_grip_weapon"]["on"]["RightHandMiddle1_JNT"] = {3.2178385257721, 27.142356872559, 2.3389554023743}
handPositions["right_grip_weapon"]["on"]["RightHandMiddle2_JNT"] = {-27.03067779541, 137.21624755859, -2.6563432216644}
handPositions["right_grip_weapon"]["on"]["RightHandMiddle3_JNT"] = {9.8199949264526, 49.10147857666, 6.0561528205872}
handPositions["right_grip_weapon"]["on"]["RightHandRing1_JNT"] = {-18.489757537842, 24.656513214111, 2.7107570171356}
handPositions["right_grip_weapon"]["on"]["RightHandRing2_JNT"] = {-37.691459655762, 128.56390380859, -15.069981575012}
handPositions["right_grip_weapon"]["on"]["RightHandRing3_JNT"] = {5.6724443435669, 63.004455566406, 5.0134029388428}
handPositions["right_grip_weapon"]["on"]["RightHandPinky1_JNT"] = {-30.595754623413, 12.251677513123, 1.6611989736557}
handPositions["right_grip_weapon"]["on"]["RightHandPinky2_JNT"] = {-58.668590545654, 75.370979309082, -16.722003936768}
handPositions["right_grip_weapon"]["on"]["RightHandPinky3_JNT"] = {-21.26050567627, 28.24077796936, 20.569374084473}

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
handPositions["left_trigger"]["on"]["LeftHandIndex1_JNT"] = {7.3522610664368, 68.381980895996, 3.1531522274017}
handPositions["left_trigger"]["on"]["LeftHandIndex2_JNT"] = {5.532808303833, 85.33113861084, 39.128715515137}
handPositions["left_trigger"]["on"]["LeftHandIndex3_JNT"] = {4.436897277832, 46.554878234863, 2.77498960495}
handPositions["left_trigger"]["off"] = {}
-- handPositions["left_trigger"]["off"]["LeftHandIndex1_JNT"] = {-7.6476874351501, 18.381666183472, 3.1531648635864}
-- handPositions["left_trigger"]["off"]["LeftHandIndex2_JNT"] = {5.5328216552734, 10.331413269043, 4.1289820671082}
-- handPositions["left_trigger"]["off"]["LeftHandIndex3_JNT"] = {4.4370613098145, -3.4452188014984, 2.7750282287598}
handPositions["left_trigger"]["off"]["LeftHandIndex1_JNT"] = {5.4113330841064, 10.378183364868, 0.91737693548203}
handPositions["left_trigger"]["off"]["LeftHandIndex2_JNT"] = {1.4339435100555, 23.6731300354, 0.97924590110779}
handPositions["left_trigger"]["off"]["LeftHandIndex3_JNT"] = {0.0, 0.0, 0.0}


handPositions["left_grip"] = {}
handPositions["left_grip"]["on"] = {}
handPositions["left_grip"]["on"]["LeftHandThumb1_JNT"] = {29.340257644653, 44.388729095459, 65.486610412598}
handPositions["left_grip"]["on"]["LeftHandThumb2_JNT"] = {-10.788741111755, 38.845840454102, 13.92786693573}
handPositions["left_grip"]["on"]["LeftHandThumb3_JNT"] = {-1.5473041534424, 55.412521362305, 0.33832994103432}
-- ["LeftHandIndex1_JNT"] = {7.3522610664368, 68.38200378418, 3.1531517505646}
-- ["LeftHandIndex2_JNT"] = {5.5328149795532, 85.331115722656, 39.12869644165}
-- ["LeftHandIndex3_JNT"] = {4.4368906021118, 46.554878234863, 2.7749688625336}
handPositions["left_grip"]["on"]["LeftHandMiddle1_JNT"] = {6.8412265777588, 64.58470916748, 1.9040077924728}
handPositions["left_grip"]["on"]["LeftHandMiddle2_JNT"] = {20.271146774292, 125.23129272461, -20.437471389771}
handPositions["left_grip"]["on"]["LeftHandMiddle3_JNT"] = {-31.853643417358, 61.327411651611, 4.9035506248474}
handPositions["left_grip"]["on"]["LeftHandRing1_JNT"] = {18.182222366333, 65.185745239258, 17.630146026611}
handPositions["left_grip"]["on"]["LeftHandRing2_JNT"] = {22.592166900635, 127.84069824219, -4.5729055404663}
handPositions["left_grip"]["on"]["LeftHandRing3_JNT"] = {-30.259429931641, 61.115940093994, -5.0124778747559}
handPositions["left_grip"]["on"]["LeftHandPinky1_JNT"] = {23.526550292969, 61.105476379395, 5.9131298065186}
handPositions["left_grip"]["on"]["LeftHandPinky2_JNT"] = {37.618221282959, 102.68962097168, 55.912754058838}
handPositions["left_grip"]["on"]["LeftHandPinky3_JNT"] = {14.95126247406, 57.935623168945, -1.4867918491364}
handPositions["left_grip"]["off"] = {}
handPositions["left_grip"]["off"]["LeftHandThumb1_JNT"] = {44.386615753174, 22.437032699585, 76.045318603516}
handPositions["left_grip"]["off"]["LeftHandThumb2_JNT"] = {-4.0847191810608, 18.195880889893, 11.097414016724}
handPositions["left_grip"]["off"]["LeftHandThumb3_JNT"] = {0.0, 0.0, 0.0}
-- handPositions["left_grip"]["off"]["LeftHandIndex1_JNT"] = {5.4113330841064, 10.378183364868, 0.91737693548203}
-- handPositions["left_grip"]["off"]["LeftHandIndex2_JNT"] = {1.4339435100555, 23.6731300354, 0.97924590110779}
-- handPositions["left_grip"]["off"]["LeftHandIndex3_JNT"] = {0.0, 0.0, 0.0}
handPositions["left_grip"]["off"]["LeftHandMiddle1_JNT"] = {-5.9782662391663, 2.1833848953247, 4.0906004905701}
handPositions["left_grip"]["off"]["LeftHandMiddle2_JNT"] = {28.419492721558, 74.716064453125, -27.526908874512}
handPositions["left_grip"]["off"]["LeftHandMiddle3_JNT"] = {0.0, 0.0, 0.0}
handPositions["left_grip"]["off"]["LeftHandRing1_JNT"] = {3.376749753952, 4.1980848312378, 7.3918490409851}
handPositions["left_grip"]["off"]["LeftHandRing2_JNT"] = {45.110187530518, 79.523498535156, -17.716432571411}
handPositions["left_grip"]["off"]["LeftHandRing3_JNT"] = {0.0, 0.0, 0.0}
handPositions["left_grip"]["off"]["LeftHandPinky1_JNT"] = {9.5718126296997, 3.7818431854248, 1.7374849319458}
handPositions["left_grip"]["off"]["LeftHandPinky2_JNT"] = {23.376853942871, 30.072662353516, -5.2138328552246}
handPositions["left_grip"]["off"]["LeftHandPinky3_JNT"] = {0.0, 0.0, 0.0}
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
