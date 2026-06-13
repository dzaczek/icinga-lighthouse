-- Databases for the Icinga DB stack (run once, on first MariaDB init).

-- Icinga DB (state synced from redis by the icingadb daemon; read by icingadb-web)
CREATE DATABASE IF NOT EXISTS icingadb CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS 'icingadb'@'%' IDENTIFIED BY 'icingadb';
GRANT ALL ON icingadb.* TO 'icingadb'@'%';

-- Icinga Web 2 (config / auth backend)
CREATE DATABASE IF NOT EXISTS icingaweb CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS 'icingaweb'@'%' IDENTIFIED BY 'icingaweb';
GRANT ALL ON icingaweb.* TO 'icingaweb'@'%';

FLUSH PRIVILEGES;
