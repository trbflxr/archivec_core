#include "archiver.hpp"
#include "util/messagebox.hpp"

Archiver::Archiver() {
    files_ = new std::vector<fs::path>();
    titles_ = new std::vector<std::string>();
}

Archiver::~Archiver() {
    delete files_;
    delete titles_;
}

void Archiver::init_dir(std::string_view dir_name, std::string_view root_dir_name) {
    fs::path dir(dir_name.data());

    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        Message::message_box("Is not a directory ", "Error", dir_name.data());
        exit(EXIT_FAILURE);
    }

    auto iter = fs::directory_iterator(dir);
    auto end = fs::directory_iterator();

    while (iter != end) {
        if (!fs::is_regular_file(iter->path())) {
            init_dir(iter->path().string(), root_dir_name);
            ++iter;
            continue;
        }

        files_->push_back(iter->path());

        auto full_path = iter->path().string();

        auto path_from_root = full_path.substr(root_dir_name.length(), full_path.length());

        titles_->push_back(path_from_root);
        ++iter;
    }
}

void Archiver::crush(std::string_view out_file_name) {

    if (titles_->size() <= 0) {
        Message::message_box("Nothing to archivate!", "Message");
        return;
    }

    fs::path out_path(out_file_name.data());

    //create dir
    mkdir(out_path);

    if (!check_replace(out_path)) return;

    FILE *out = fopen(out_file_name.data(), "wb");

    if (out == nullptr) {
        Message::message_box("Couldn't create file ", "Error", out_file_name.data());
        exit(EXIT_FAILURE);
    }

    Stamp stamp;
    stamp.files_count = files_->size();

    //write stamp
    fwrite(&stamp, sizeof(stamp), 1, out);

    //archiving
    std::cout << "Archiving: " << std::endl;

    for (auto i = 0; i < titles_->size(); ++i) {
        Entry entry;
        entry.id = i;
        entry.size = fs::file_size(files_->at(i));
        entry.name_length = titles_->at(i).length();

        //write entry to crushed file
        fwrite(&entry, sizeof(Entry), 1, out);

        //write filename
        fwrite(titles_->at(i).c_str(), titles_->at(i).length(), 1, out);

        //split file

        //open file and read
        uint8_t *contents = new uint8_t[entry.size + 1];
        FILE *in = fopen(files_->at(i).string().c_str(), "rb");

        if (in == nullptr) {
            Message::message_box("Couldn't access file ", "Error", titles_->at(i));
            exit(EXIT_FAILURE);
        }

        std::cout << "\t->" << titles_->at(i) << std::endl;
        for (auto j = 0; j < entry.size; j++) {
            fread(contents + j, sizeof(uint8_t), 1, in);
            fwrite(contents + j, sizeof(uint8_t), 1, out);
        }

        delete[] contents;
        fclose(in);
    }
    fclose(out);

}


void Archiver::add_to_existing_archive(std::vector<std::string_view> &file_paths, std::string_view existing_archive) {

    FILE *out = fopen(existing_archive.data(), "rw+b");

    titles_->clear();
    files_->clear();
    add_to_archive(file_paths);

    if (out == nullptr) {
        if (Message::message_box_yes_no("file does not exist, create it?\n", "Message", existing_archive.data())) {
            crush(existing_archive);
            return;
        } else {
            exit(EXIT_FAILURE);
        }
    }

    //read stamp
    Stamp stamp;
    memset(&stamp, 0, sizeof(Stamp));
    fread(&stamp, sizeof(Stamp), 1, out);

    fseek(out, 0, SEEK_END);

    for (int i = 0; i < titles_->size(); ++i) {
        Entry entry;
        entry.id = i;
        entry.size = fs::file_size(files_->at(i));
        entry.name_length = titles_->at(i).length();

        fwrite(&entry, sizeof(Entry), 1, out);

        fwrite(titles_->at(i).data(), titles_->at(i).length(), 1, out);

        uint8_t *contents = new uint8_t[entry.size + 1];
        FILE *in = fopen(files_->at(i).string().c_str(), "rb");

        if (in == nullptr) {
            Message::message_box("Couldn't access file ", "Error", titles_->at(i));
            file_paths.pop_back();
        }

        std::cout << "\t->" << titles_->at(i) << std::endl;
        for (auto j = 0; j < entry.size; j++) {
            fread(contents + j, sizeof(uint8_t), 1, in);
            fwrite(contents + j, sizeof(uint8_t), 1, out);
        }

        delete[] contents;
        fclose(in);
    }

    //write new files count
    stamp.files_count += titles_->size();
    fseek(out, 0, SEEK_SET);
    fwrite(&stamp, sizeof(Stamp), 1, out);

    fclose(out);
}

void Archiver::add_to_archive(std::vector<std::string_view> &files) {
    for (auto &&path : files) {
        fs::path path_to_file(path.data());

        if (fs::exists(path_to_file)) {
            if (fs::is_regular_file(path_to_file)) {
                files_->push_back(path_to_file);
                titles_->push_back(path_to_file.filename().string());
            } else {
                init_dir(path, path_to_file.end()->string());
            }
        } else {
            Message::message_box("File or directory is not exists ", "Warning", path.data());
        }

    }
}

bool Archiver::check_stamp(const Stamp &stamp) {
    return stamp.x == 0x52 && stamp.y == 0x84 && stamp.z == 0x91;
}

bool Archiver::extract_file(std::string_view title, std::string_view dest_path, const uint32_t file_id) {
    bool found = false;

    FILE *in = fopen(title.data(), "rb");

    if (in == nullptr) {
        Message::message_box("Couldn't access file ", "Error", title.data());
        exit(EXIT_FAILURE);
    }

    Stamp stamp;
    memset(&stamp, 0, sizeof(Stamp));

    fread(&stamp, sizeof(Stamp), 1, in);

    if (!check_stamp(stamp)) {
        Message::message_box("Unknown file format!", "Error");
        exit(EXIT_FAILURE);
    }

    for (auto i = 0; i < stamp.files_count; i++) {
        Entry entry;
        memset(&entry, 0, sizeof(Entry));

        fread(&entry, sizeof(Entry), 1, in);

        if (entry.id != file_id) {
            fseek(in, entry.name_length + entry.size, SEEK_CUR);
            continue;
        }

        std::cout << "Extracting: " << std::endl;

        //file title
        uint8_t *title = new uint8_t[entry.name_length + 1];
        fread(title, entry.name_length, 1, in);
        title[entry.name_length] = '\0';

        std::cout << "\t->" << title << std::endl;

        //create dir
        fs::path out_path(dest_path.data());
        mkdir(out_path);

        if (check_replace(out_path)) {

            //split file
            FILE *out = fopen(dest_path.data(), "wb");
            if (out == nullptr) {
                Message::message_box("Couldn't write extracted file!\n", "Error", out_path.filename().string());
                exit(EXIT_FAILURE);
            }

            //file content
            uint8_t *contents = new uint8_t[entry.size + 1];
            fread(contents, entry.size, 1, in);
            fwrite(contents, entry.size, 1, out);
            contents[entry.size] = '\0';

            delete[] contents;
            fclose(out);
        }
        delete[] title;
        found = true;
        break;
    }

    fclose(in);

    return found ? found : false;
}

void Archiver::extract(std::string_view title, std::string_view dest_path) {

    FILE *in = fopen(title.data(), "rb");

    if (in == nullptr) {
        Message::message_box("Couldn't access file ", "Error", title.data());
        exit(EXIT_FAILURE);
    }

    Stamp stamp;
    memset(&stamp, 0, sizeof(Stamp));
    fread(&stamp, sizeof(Stamp), 1, in);

    if (!check_stamp(stamp)) {
        Message::message_box("Unknown file format!", "Error");
        exit(EXIT_FAILURE);
    }

    //create dir
    fs::path out_path(dest_path.data());
    mkdir(out_path);

    std::cout << "Extracting: " << std::endl;
    for (auto i = 0; i < stamp.files_count; i++) {
        Entry entry;
        memset(&entry, 0, sizeof(Entry));

        fread(&entry, sizeof(Entry), 1, in);

        //file title
        uint8_t *title = new uint8_t[entry.name_length + 1];
        fread(title, entry.name_length, 1, in);
        title[entry.name_length] = '\0';

        std::cout << "\t->" << title << std::endl;

        fs::path path_to_file(dest_path.data());
        path_to_file.append(reinterpret_cast<const char *>(title));

        mkdir(path_to_file);

        if (check_replace(path_to_file)) {
            //split file
            FILE *out = fopen(path_to_file.string().c_str(), "wb");
            if (out == nullptr) {
                Message::message_box("Couldn't write extracted file!", "Error");
                exit(EXIT_FAILURE);
            }

            //file content
            uint8_t *contents = new uint8_t[entry.size + 1];
            fread(contents, entry.size, 1, in);
            fwrite(contents, entry.size, 1, out);
            contents[entry.size] = '\0';

            fclose(out);
            delete[] contents;
        } else {
            fseek(in, entry.size, SEEK_CUR);
        }
        delete[] title;
    }

    fclose(in);
}

std::vector<ArchivedFile> Archiver::extract_files_info(std::string_view title) {
    std::vector<ArchivedFile> entries;

    FILE *in = fopen(title.data(), "rb");

    if (in == nullptr) {
        Message::message_box("Couldn't access file ", "Error", title.data());
        exit(EXIT_FAILURE);
    }

    Stamp stamp;
    memset(&stamp, 0, sizeof(Stamp));

    fread(&stamp, sizeof(Stamp), 1, in);

    if (!check_stamp(stamp)) {
        Message::message_box("Unknown file format!", "Error");
        exit(EXIT_FAILURE);
    }


    for (auto i = 0; i < stamp.files_count; i++) {
        Entry entry;
        memset(&entry, 0, sizeof(Entry));
        fread(&entry, sizeof(Entry), 1, in);

        uint8_t *name = new uint8_t[entry.name_length + 1];
        fread(name, entry.name_length, 1, in);
        name[entry.name_length] = '\0';

        ArchivedFile file;
        file.id = entry.id;
        file.size = entry.size;
        file.name = name;

        entries.push_back(file);

        fseek(in, entry.size, SEEK_CUR);
    }

    fclose(in);
    return entries;
}


uint32_t Archiver::extract_files_count(std::string_view title) {
    FILE *in = fopen(title.data(), "r");
    if (in == nullptr) {
        Message::message_box("Couldn't access file ", "Error", title.data());
        exit(EXIT_FAILURE);
    }

    Stamp stamp;

    memset(&stamp, 0, sizeof(Stamp));
    fread(&stamp, sizeof(Stamp), 1, in);

    if (!check_stamp(stamp)) {
        Message::message_box("Unknown file format!", "Error");
        exit(EXIT_FAILURE);
    }

    return stamp.files_count;
}


void Archiver::mkdir(fs::path &path) {
    //create directory
    if (path.branch_path() != "") {

        if (fs::exists(path.branch_path())) return;

        if (!fs::create_directories(path.branch_path())) {
            Message::message_box("Couldn't create directory ", "Error", path.branch_path().string());
            exit(EXIT_FAILURE);
        }
    }
}

bool Archiver::check_replace(fs::path &path) {
    if (fs::exists(path) && fs::is_regular_file(path)) {
        if (Message::message_box_yes_no("File is already exists! Replace it?\n", "Message",
                                        path.filename().string())) {
            fs::remove_all(path);
            return true;
        }
        return false;
    }
}
