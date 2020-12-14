DROP TABLE IF EXISTS injection;
CREATE TABLE injection (
         `instr1` int(10) unsigned NOT NULL,
         `instr2` int(10) unsigned NOT NULL,
         `duration` int(10) unsigned NOT NULL,
         `data_address` int(10) unsigned NOT NULL,
         `bitoffset` int(10) unsigned NOT NULL,
         `resulttype` enum('WRITE_TEXTSEGMENT','WRITE_OUTERSPACE','TRAP','OK_MARKER','TIMEOUT','FAIL_MARKER') NOT NULL,
         PRIMARY KEY (`data_address`,`instr2`,`bitoffset`)
        );

INSERT INTO injection VALUES
;