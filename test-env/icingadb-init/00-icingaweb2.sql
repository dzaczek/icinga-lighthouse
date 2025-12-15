-- Create Icinga Web 2 config/auth database + user
-- NOTE: This runs only on first init of the MariaDB volume.

CREATE DATABASE IF NOT EXISTS icingaweb2;

CREATE USER IF NOT EXISTS 'icingaweb2'@'%' IDENTIFIED BY 'icingaweb2';
GRANT ALL PRIVILEGES ON icingaweb2.* TO 'icingaweb2'@'%';

FLUSH PRIVILEGES;
