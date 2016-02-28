-- This is the schema used by BitlBee's MySQL storage backend

CREATE TABLE users (
    `login`          VARCHAR(32) NOT NULL,
    `password`       VARCHAR(128) NOT NULL,
    `auth_backend`   VARCHAR(64) NOT NULL,
    PRIMARY KEY (`login`)
);

CREATE TABLE settings (
    `login`   VARCHAR(32) NOT NULL,
    `setting` VARCHAR(32) NOT NULL,
    `value`   VARCHAR(64) NOT NULL,
    `locked`  TINYINT(1) NOT NULL DEFAULT 0,
    PRIMARY KEY (`login`, `setting`),
    INDEX `login` (`login`)
);

CREATE TABLE accounts (
    `login`        VARCHAR(32) NOT NULL,
    `account`      VARCHAR(64) NOT NULL,
    `protocol`     VARCHAR(16) NOT NULL,
    `password`     VARCHAR(128) NOT NULL,
    `tag`          VARCHAR(32) NOT NULL,
    `server`       VARCHAR(64) NOT NULL DEFAULT "",
    `auto_connect` TINYINT(1) NOT NULL DEFAULT 0,
    `locked`       TINYINT(1) NOT NULL DEFAULT 0,
    PRIMARY KEY (`login`, `account`),
    INDEX `login` (`login`)
);

CREATE TABLE account_settings (
    `login`   VARCHAR(32) NOT NULL,
    `account` VARCHAR(64) NOT NULL,
    `setting` VARCHAR(32) NOT NULL,
    `value`   VARCHAR(64) NOT NULL,
    `locked`  TINYINT(1) NOT NULL DEFAULT 0,
    PRIMARY KEY (`login`, `account`, `setting`),
    INDEX `login` (`login`, `account`)
);

CREATE TABLE channel_settings (
    `login`   VARCHAR(32) NOT NULL,
    `channel` VARCHAR(64) NOT NULL,
    `setting` VARCHAR(32) NOT NULL,
    `value`   VARCHAR(64) NOT NULL,
    `locked`  TINYINT(1) NOT NULL DEFAULT 0,
    PRIMARY KEY (`login`, `channel`, `setting`),
    INDEX `login` (`login`)
);

CREATE TABLE buddies (
    `login`  VARCHAR(32) NOT NULL,
    `account`VARCHAR(64) NOT NULL,
    `handle` VARCHAR(64) NOT NULL,
    `nick`   VARCHAR(64) NOT NULL,
    PRIMARY KEY (`login`, `account`, `handle`),
    INDEX `login` (`login`, `account`)
);
