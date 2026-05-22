# SimpleFS: отчет по лабораторной работе

## Цель работы

Цель работы вы можете увидеть в файле [ТУТ (ТЫК)](Linux_Kernel_Proposal.md)

## Краткое описание реализации

Superblock хранится в двух копиях. Смещения задаются параметрами sb_offset1 и sb_offset2. При загрузке модуля выполняется чтение обеих копий; если валидная копия не найдена, то устройство форматируется

Каждый файл получает фиксированный диапазон из M секторов:

```text
file_start = data_start + index * max_file_sectors
file_capacity = max_file_sectors * 512 - 128
```

Первые 128 байт первого сектора файла заняты metadata, но для userspace offset 0 соответствует первому байту пользовательских данных, так что можно сказать, что пользователь этого не видит 

Текущая длина пользовательских данных хранится отдельно в metadata, поэтому обычные операции типа `>` и `>>` работают с логическим размером файла (с append было нелегко разобраться, но оно работает).

## Параметры модуля для того, чтоб вы могли сделать insmode. Можете подробнее почитать в условии

| Параметр | Описание |
| --- | --- |
| disk_name | Блочное устройство, например /dev/loop1000 |
| sb_offset1 | Сектор первой копии superblock |
| sb_offset2 | Сектор второй копии superblock |
| max_filename_len | Максимальная длина имени файла |
| max_file_sectors | Размер одного файла в секторах |

Пример запуска insmod: (Ниже я приведу нормальный полный пример, как можно собрать)

```bash
sudo insmod kernel/simplefs.ko \
  disk_name=/dev/loop0 \
  sb_offset1=0 \
  sb_offset2=1 \
  max_filename_len=32 \
  max_file_sectors=2
```


## IOCTL

Поддерживаются команды:

- `SIMPLEFS_IOC_ZERO_FILES` — обнулить данные всех файлов;
- `SIMPLEFS_IOC_WIPE_FS` — заново отформатировать всю FS;
- `SIMPLEFS_IOC_GET_META` — получить список metadata/размеров/CRC по файлам;
- `SIMPLEFS_IOC_GET_SECTOR_MAP` — получить список секторов для заданного файла.

```bash
./userspace/simplefs_test /mnt/simplefs_test
./userspace/simplefs_test /mnt/simplefs_test zero
./userspace/simplefs_test /mnt/simplefs_test wipe
./userspace/simplefs_test /mnt/simplefs_test meta
./userspace/simplefs_test /mnt/simplefs_test map file1337
```

## Сборка

Сборку нужно выполнять на системе, где будет загружаться модуль, а именно, чтобы ядро было версии 6.12.*, так как header's у всех ядер разные.

Требования:

- Linux kernel `6.12.x`;
- `linux-headers-$(uname -r)`;
- `build-essential`;
- права root для `insmod`, `mount`, `losetup`.

Команды:

```bash
make
```

Очистка:

```bash
make clean
```

## Ручное тестирование

### 1. Создать loop image

```bash
truncate -s 4M /tmp/simplefs.img
sudo losetup -f --show /tmp/simplefs.img
```

Команда выведет устройство, например /dev/loop100.

### 2. Собрать проект

```bash
make
```

### 3. Загрузить модуль. Главное - подправьте disk_name, иначе не заведется

```bash
sudo insmod kernel/simplefs.ko \
  disk_name=/dev/loop100 \ 
  sb_offset1=0 \
  sb_offset2=1 \
  max_filename_len=32 \
  max_file_sectors=1
```

### 4. Смонтировать файловую систему

```bash
sudo mkdir -p /mnt/simplefs_test
sudo mount -t simplefs /dev/loop100 /mnt/simplefs_test
ls -la /mnt/simplefs_test
```

### 5. Проверить read/write

```bash
./userspace/simplefs_test /mnt/simplefs_test
```

Ожидаемый результат: для каждого файла выводится строка вида `OK`.

### 6. Проверить ioctl

```bash
./userspace/simplefs_test /mnt/simplefs_test meta
./userspace/simplefs_test /mnt/simplefs_test map file0000
./userspace/simplefs_test /mnt/simplefs_test zero
./userspace/simplefs_test /mnt/simplefs_test wipe
```

После `wipe` можно перемонтировать ФС и проверить, что файлы снова доступны:

```bash
sudo umount /mnt/simplefs_test
sudo mount -t simplefs /dev/loop0 /mnt/simplefs_test
ls /mnt/simplefs_test | head
```

### 7. Выгрузить модуль и отключить loop

```bash
sudo umount /mnt/simplefs_test
sudo rmmod simplefs
sudo losetup -d /dev/loop0
```

## Автоматизированный smoke test, который я добросовестно нагенерировал, чтобы найти отловить все баги  👍 👍 👍

Скрипт `scripts/test_mount.sh` выполняет сборку, загрузку модуля, mount, demo read/write, ioctl-проверки и cleanup.

```bash
./scripts/setup_loop.sh
./scripts/test_mount.sh
```

Проверка `max_file_sectors=2`:

```bash
SIMPLEFS_MAX_FILE_SECTORS=2 ./scripts/test_mount.sh
```