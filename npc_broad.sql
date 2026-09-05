/*
Navicat MySQL Data Transfer

Source Server         : eric
Source Server Version : 50522
Source Host           : localhost:3306
Source Database       : renewal_381

Target Server Type    : MYSQL
Target Server Version : 50522
File Encoding         : 65001

Date: 2026-08-30 19:18:18
*/

SET FOREIGN_KEY_CHECKS=0;
-- ----------------------------
-- Table structure for `npc_broad`
-- ----------------------------
DROP TABLE IF EXISTS `npc_broad`;
CREATE TABLE `npc_broad` (
  `id` int(10) NOT NULL AUTO_INCREMENT COMMENT '資料序號',
  `npc_id` int(10) DEFAULT NULL COMMENT 'NPC編號',
  `npc_note` varchar(500) DEFAULT NULL COMMENT 'npc名稱',
  `send_type` int(10) DEFAULT '1' COMMENT '發送類型1:BOSS用上世界頻道2:一般喊話內容',
  `death_chat` varchar(1000) DEFAULT NULL COMMENT '死亡喊話訊息',
  `sort` varchar(10) DEFAULT NULL COMMENT 'boss分類，填入S/A/B/C/D，S=S級BOSS',
  `no_damage` int(2) DEFAULT '0' COMMENT '是否受傷動作改為噴血特效',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB AUTO_INCREMENT=385 DEFAULT CHARSET=utf8;

-- ----------------------------
-- Records of npc_broad
-- ----------------------------
INSERT INTO npc_broad VALUES ('108', '45338', '巨大鱷魚', '1', null, 'B', '0');
INSERT INTO npc_broad VALUES ('109', '45529', '飛龍', '1', null, 'C', '0');
INSERT INTO npc_broad VALUES ('113', '45545', '黑長者', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('114', '45617', '不死鳥', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('115', '45577', '突擊旅長．闇黑劍士', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('116', '45844', '巴蘭卡', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('117', '45607', '魔獸團長．凱巴勒', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('118', '45588', '魔獸師長．辛克萊', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('119', '45602', '魔法團長．卡勒米爾', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('120', '45863', '法令軍王．蕾雅', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('121', '45612', '神官長．邦妮', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('122', '45608', '傭兵隊長．麥帕斯托', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('123', '45615', '冥法團長．可利波斯', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('124', '45676', '冥法軍王．海露拜', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('125', '45648', '暗殺軍王．史雷佛', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('126', '45574', '親衛隊長．凱特', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('127', '45585', '暗殺團長．佈雷哲', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('128', '45625', '混沌', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('129', '45674', '死亡', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('130', '45675', '火焰之影', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('131', '45955', '長老．琪娜', '1', null, 'A', '0');
INSERT INTO npc_broad VALUES ('132', '45956', '長老．巴塔斯', '1', null, 'A', '0');
INSERT INTO npc_broad VALUES ('133', '45957', '長老．巴洛斯', '1', null, 'A', '0');
INSERT INTO npc_broad VALUES ('134', '45958', '長老．安迪斯', '1', null, 'A', '0');
INSERT INTO npc_broad VALUES ('135', '45959', '長老．艾迪爾', '1', null, 'A', '0');
INSERT INTO npc_broad VALUES ('136', '45960', '長老．泰瑪斯', '1', null, 'A', '0');
INSERT INTO npc_broad VALUES ('137', '45961', '長老．拉曼斯', '1', null, 'A', '0');
INSERT INTO npc_broad VALUES ('138', '45962', '長老．巴陸德', '1', null, 'A', '0');
INSERT INTO npc_broad VALUES ('139', '45963', '副神官．卡山德拉', '1', null, 'A', '0');
INSERT INTO npc_broad VALUES ('140', '81163', '吉爾塔斯', '1', null, 'S', '0');
INSERT INTO npc_broad VALUES ('141', '45601', '死亡騎士', '1', null, 'A', '0');
INSERT INTO npc_broad VALUES ('142', '45583', '巴列斯', '1', null, 'B', '0');
INSERT INTO npc_broad VALUES ('143', '45682', '波魔斯', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('144', '45931', '水之精靈', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('145', '45942', '受詛咒的 水精靈王', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('146', '45941', '受詛咒的巫女莎爾', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('147', '45943', '卡普', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('148', '45944', '巨大蜈蚣', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('149', '45683', '法利昂', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('150', '45684', '巴拉卡斯', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('151', '45584', '巨大牛人', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('152', '46142', '冰魔', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('153', '46141', '冰之女王', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('154', '45649', '惡魔', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('155', '45513', '扭曲的潔尼斯女王', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('156', '45547', '不幸的幻象眼魔', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('157', '45606', '恐怖的吸血鬼', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('158', '45650', '死亡的殭屍王', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('159', '45653', '不死的木乃伊王', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('160', '45654', '冷酷的艾莉絲', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('161', '45664', '闇黑的騎士范德', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('162', '45672', '不滅的巫妖', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('163', '45673', '邪惡的鐮刀死神', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('164', '45671', '阿利歐克', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('165', '45795', '樹精', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('166', '46025', '塔洛斯伯爵', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('167', '46024', '伯爵的親衛隊', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('168', '46026', '曼孟', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('169', '45828', '變形怪', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('170', '45829', '巴貝多', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('171', '45801', '瑪依奴夏門的鑽石高崙', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('172', '45802', '瑪依奴夏門', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('173', '46037', '瑪雅', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('174', '45642', '土精靈王', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('175', '45643', '水精靈王', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('176', '45644', '風精靈王', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('177', '45645', '火精靈王', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('178', '45573', '巴風特', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('179', '45685', '墮落', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('180', '45646', '深淵之主', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('181', '45492', '庫曼', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('182', '45548', '豪勢', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('183', '46013', '拉斯塔巴德近衛隊隊長', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('184', '45735', '半魚人首領', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('185', '45734', '大王烏賊', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('186', '45752', '炎魔', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('188', '45488', '卡士伯', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('189', '45478', '魔法師', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('199', '45578', '飛龍', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('200', '45516', '伊弗利特', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('201', '45546', '變形怪首領', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('202', '45610', '古代巨人', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('203', '45652', '地獄的黑豹', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('204', '45535', '曼波兔', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('205', '45534', '曼波兔', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('206', '97259', '沙蟲', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('207', '97143', '烏若庫斯', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('208', '97149', '阿勒尼亞', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('209', '97120', '力卡溫', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('210', '97130', '凱巴雷', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('211', '45600', '克特', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('212', '97137', '奈克偌斯', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('213', '97194', '大腳的瑪幽', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('214', '45614', '巨蟻女皇', '1', null, null, '0');
INSERT INTO npc_broad VALUES ('220', '99600', '強盜頭目#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('221', '99601', '巨大鱷魚#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('222', '99602', '變種巨蟻女皇#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('223', '99603', '魔法師#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('224', '99604', '卡士伯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('225', '99605', '庫曼#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('226', '99606', '伊弗利特#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('227', '99607', '飛龍#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('228', '99608', '曼波兔#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('229', '99609', '曼波兔#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('230', '99610', '黑長者#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('231', '99611', '變形怪首領#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('232', '99612', '巴風特#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('233', '99613', '親衛隊長．凱特#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('234', '99614', '突擊旅長．闇黑劍士#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('235', '99615', '飛龍#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('236', '99616', '巴列斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('237', '99617', '巨大牛人#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('238', '99618', '暗殺團長．布雷哲#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('239', '99619', '克特#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('240', '99620', '死亡騎士#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('241', '99621', '魔法團長．卡勒米爾#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('242', '99622', '魔獸團長．凱巴勒#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('243', '99623', '傭兵隊長．麥帕斯托#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('244', '99624', '古代巨人#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('245', '99625', '神官長．邦妮#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('246', '99626', '巨蟻女皇#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('247', '99627', '冥法團長．可利波斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('248', '99628', '不死鳥#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('249', '99629', '混沌#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('250', '99630', '土精靈王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('251', '99631', '水精靈王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('252', '99632', '風精靈王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('253', '99633', '火精靈王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('254', '99634', '深淵之主#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('255', '99635', '暗殺軍王．史雷佛#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('256', '99636', '惡魔#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('257', '99637', '阿利歐克#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('258', '99638', '死亡#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('259', '99639', '火焰之影#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('260', '99640', '冥法軍王．海露拜#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('261', '99641', '恐怖的 法利昂#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('262', '99642', '恐怖的 巴拉卡斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('263', '99643', '墮落#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('264', '99644', '大王烏賊#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('265', '99645', '半魚人首領#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('266', '99646', '炎魔#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('267', '99647', '炎魔#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('268', '99648', '污濁 妖魔戰士#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('269', '99649', '樹精#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('270', '99650', '瑪依奴夏門的鑽石高崙#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('271', '99651', '瑪依奴夏門#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('272', '99652', '變形怪#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('273', '99653', '巴貝多#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('274', '99654', '魔獸軍王．巴蘭卡#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('275', '99655', '法令軍王．蕾雅#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('276', '99656', '水之精靈#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('277', '99657', '受詛咒的 梅杜莎#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('278', '99658', '受詛咒的巫女莎爾#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('279', '99659', '受詛咒的 水精靈王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('280', '99660', '卡普#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('281', '99661', '巨大蜈蚣#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('282', '99662', '長老．琪娜#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('283', '99663', '長老．巴塔斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('284', '99664', '長老．巴洛斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('285', '99665', '長老．安迪斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('286', '99666', '長老．艾迪爾#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('287', '99667', '長老．泰瑪斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('288', '99668', '長老．拉曼斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('289', '99669', '長老．巴陸德#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('290', '99670', '副神官．卡山德拉#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('291', '99671', '伯爵的親衛隊#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('292', '99672', '塔洛斯伯爵#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('293', '99673', '曼孟#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('294', '99674', '腐爛的 殭屍王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('295', '99675', '腐爛的 骷髏騎士#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('296', '99676', '瑪雅#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('297', '99677', '底比斯 賀洛斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('298', '99678', '底比斯 阿努比斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('299', '99679', '冰之女王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('300', '99680', '冰魔#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('301', '99681', '火焰之影#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('302', '99682', '吉爾塔斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('303', '99683', '提卡爾 杰弗雷庫#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('304', '99684', '提卡爾 杰弗雷庫#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('305', '99685', '強盜首領克萊恩#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('306', '99686', '浮士德#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('307', '99687', '力卡溫#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('308', '99688', '凱巴雷#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('309', '99689', '奈克偌斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('310', '99690', '烏若庫斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('311', '99691', '阿勒尼亞#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('312', '99692', '萬年史萊姆#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('313', '99693', '腐蝕的 骷髏騎士#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('314', '99694', '巴風特的追蹤者#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('315', '99695', '狂風的夏斯奇(紅)#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('316', '99696', '狂風的夏斯奇(綠)#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('317', '99697', '大腳的瑪幽#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('318', '99698', '紅色妖魔#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('319', '99699', '閃爍之星#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('320', '99700', '沙蟲#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('321', '99701', '黑魔法師#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('322', '99702', '哈汀之影#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('323', '99703', '巨型骷髏#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('324', '99704', '蒼暮之星#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('325', '99705', '風龍的謝托雷#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('326', '99706', '歪曲的<阿拉克妮>#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('327', '99707', '不幸的<思提志>#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('328', '99708', '恐怖的<露西>#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('329', '99709', '傲慢之塔小王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('330', '99710', '傲慢之塔小王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('331', '99711', '傲慢之塔小王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('332', '99712', '傲慢之塔小王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('333', '99713', '傲慢之塔小王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('334', '99714', '傲慢之塔小王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('335', '99715', '傲慢之塔小王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('336', '99716', '傲慢之塔小王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('337', '99717', '愛恨的布魯托#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('338', '99718', '黑騎士隊長#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('339', '99719', '西瑪#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('340', '99720', '巴土瑟#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('341', '99721', '馬庫爾#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('342', '99722', '風龍的守護者', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('343', '99723', '歐吉王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('344', '99724', '哈維女皇#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('345', '99725', '亞力安王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('346', '99726', '背叛者 可羅蘭斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('347', '99727', '史派伊斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('348', '99728', '納拉庫#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('349', '99729', '地龍的圖帕#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('350', '99730', '黑騎士團長#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('351', '99731', '赤鬼#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('352', '99732', '熔岩域主#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('353', '99733', '涅槃鳳凰#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('354', '99734', '風龍之裔[伊納爾]#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('355', '99735', '獨角獸#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('356', '99736', '夢魘#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('357', '99737', '扭曲的潔尼斯女王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('358', '99738', '不幸的幻象眼魔#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('359', '99739', '恐怖的吸血鬼#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('360', '99740', '死亡的殭屍王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('361', '99741', '地獄的黑豹#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('362', '99742', '不死的木乃伊王#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('363', '99743', '冷酷的艾莉絲#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('364', '99744', '闇黑的騎士范德#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('365', '99745', '不滅的巫妖#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('366', '99746', '傲慢的烏格奴斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('367', '99747', '邪惡的鐮刀死神#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('368', '99748', '泰坦高崙#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('369', '99749', '伊娃王國軍長．涅特#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('370', '99750', '神殿守護者．托利頓#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('371', '99751', '亞克魔#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('372', '99752', '傑羅斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('373', '99753', '須曼#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('374', '99754', '牛鬼#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('375', '99755', '白面金毛九尾狐#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('376', '99756', '奇美拉爾德#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('377', '99757', '古代守護者#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('378', '99758', '吉爾塔斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('379', '99759', '恐怖的 林德拜爾#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('380', '99760', '恐怖的 安塔瑞斯#新編號', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('381', '99784', '魔化魔獸軍王．巴蘭卡', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('382', '99785', '魔化法令軍王．蕾雅', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('383', '99786', '魔化冥法軍王．海露拜', '1', null, null, '1');
INSERT INTO npc_broad VALUES ('384', '99787', '魔化暗殺軍王．史雷佛', '1', null, null, '1');
