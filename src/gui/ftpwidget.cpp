//
// Created by 10484 on 24-9-24.
//

// You may need to build the project (run Qt uic code generator) to get "ui_FtpWidget.h" resolved

#include "ftpwidget.h"
#include "ui_FtpWidget.h"
#include <string>
#include <QMessageBox>
#include <thread>

#include <QtSvg/QSvgRenderer>
#include <QPainter>

#include <QMenu>
#include <QFileDialog>
#include <filesystem>


FtpWidget::FtpWidget(QWidget *parent) : QWidget(parent), ui(new Ui::FtpWidget)
{
	ui->setupUi(this);
	ftp_core_ = std::make_unique<FtpCore>();

	init_resource();          // 初始化资源
	init_connect_sig_slots(); // 初始化信号与槽
	init_file_list();         // 初始化文件列表
	init_memu();              // 初始化右键菜单
}

FtpWidget::~FtpWidget()
{
	if (p_interrupt_login_)
		delete p_interrupt_login_;
	if (ui)
		delete ui;
	if (pixmap_)
		delete pixmap_;
}

void FtpWidget::init_connect_sig_slots()
{
	connect(ui->pushButton_start, &QPushButton::clicked, this,
	        &FtpWidget::on_pushButton_start); // 点击登录按钮，尝试登录
	connect(ui->pushButton_cancel_login, &QPushButton::clicked, this,
	        &FtpWidget::on_pushButton_cancel_login); // 点击取消登录按钮

	connect(this, &FtpWidget::sig_connect_OK, this, [this]()
	{
		/* 登陆成功 跳转到工作界面 */
		ui->stackedWidget->setCurrentIndex(2);
		QMessageBox::information(nullptr, "登录成功", "登陆成功!!");
	});
	connect(this, &FtpWidget::sig_connect_failed, this, [this]()
	{
		/* 登陆失败 跳转到登录界面 */
		ui->stackedWidget->setCurrentIndex(0);
		if (!(*p_interrupt_login_))
			QMessageBox::warning(nullptr, "登录失败", "请重新检查");
	});

	connect(this, &FtpWidget::sig_connect_OK, this,
	        &FtpWidget::on_pushButton_refresh); // 登录成功 刷新文件列表
	connect(ui->pushButton_refresh, &QPushButton::clicked, this,
	        &FtpWidget::on_pushButton_refresh); // 点击刷新按钮 刷新文件列表
	connect(this, &FtpWidget::sig_connect_OK, this, [this]()
	{
		/* 登录成功 显示当前路径 显示远程主机 用户名 端口 */
		ui->lineEdit_url_working->setText(remote_path_.c_str());
		std::string url = ftp_core_->remote_url();
		std::string user = ftp_core_->username();
		int port = ftp_core_->port();
		QString text = QString("%0:%1").arg(url.c_str()).arg(port);
		QString remote_url = text.replace("://", QString("://%0@").arg(user.c_str()));
		ui->label_remote_url->setText(remote_url);
	});

	connect(ui->lineEdit_url_working, &QLineEdit::returnPressed, this,
	        &FtpWidget::on_lineEdit_url_working); // 输入路径后回车，跳转到指定路径

	connect(ui->pushButton_cdup, &QPushButton::clicked, this,
	        &FtpWidget::on_pushButton_cdup); // 点击返回上一级目录

	connect(ui->tableWidget, &QTableWidget::cellDoubleClicked, this,
	        &FtpWidget::on_tableWidget_cellDoubleClicked); // 双击后跳转

	connect(this, &FtpWidget::sig_put_result, this, [this](const QString &msg)
	{
		/* 上传完成后，会调用此函数来报告结果 */
		QMessageBox::information(this, "上传结果", msg);
	});
	connect(this, &FtpWidget::sig_get_result, this, [this](const QString &msg)
	{
		/* 下载完成后，会调用此函数来报告结果 */
		QMessageBox::information(this, "下载结果", msg);
	});
}

void FtpWidget::init_resource()
{
	// 加载svg图像
	QString strPath = ":/img/loader.svg";
	QSvgRenderer *svg_renderer = new QSvgRenderer();
	svg_renderer->load(strPath);

	pixmap_ = new QPixmap(ui->label_loading->size());
	pixmap_->fill(Qt::transparent);
	QPainter painter(pixmap_);
	svg_renderer->render(&painter);

	ui->label_loading->setPixmap(*pixmap_);
	ui->label_loading->setAlignment(Qt::AlignHCenter);
}

void FtpWidget::init_file_list()
{
	// 列表只能有对应的7项 权限 链接数 属组 属主...
	ui->tableWidget->setColumnCount(7);
	// 设置列表表头
	// ui->tableWidget->setHorizontalHeaderLabels({"权限", "链接数", "用户", "用户组", "大小", "日期", "文件名"});
	ui->tableWidget->setHorizontalHeaderLabels({"名称", "大小", "修改日期", "权限", "用户", "用户组", "链接数"});
}

void FtpWidget::file_list_clear()
{
	while (ui->tableWidget->rowCount() > 0)
		ui->tableWidget->removeRow(0);
}

void FtpWidget::file_list_add(std::vector<file_info> &file_list)
{
	for (auto &file_info: file_list)
	{
		int new_row = ui->tableWidget->rowCount();
		ui->tableWidget->insertRow(new_row);
		file_list_item_add(new_row, 0, file_info.name);
		file_list_item_add(new_row, 1, file_info.size);
		file_list_item_add(new_row, 2, file_info.modify_time);
		file_list_item_add(new_row, 3, file_info.permissions);
		file_list_item_add(new_row, 4, file_info.user);
		file_list_item_add(new_row, 5, file_info.group);
		file_list_item_add(new_row, 6, file_info.link_number);
	}
}

void FtpWidget::file_list_item_add(int row, int idx, const std::string &item)
{
	ui->tableWidget->setItem(row, idx, new QTableWidgetItem(item.c_str()));
	// Qt::ItemIsSelectable：项目可以被选中。
	// Qt::ItemIsUserCheckable：项目可以被用户勾选或取消勾选。
	// Qt::ItemIsEnabled：项目是可交互的。
	ui->tableWidget->item(row, idx)->setFlags(Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
}

std::string FtpWidget::getParentDirectory(const std::string &path)
{
	size_t lastSlashPos = path.rfind('/');

	// 如果路径中没有'/'，返回"./"表示当前目录
	if (lastSlashPos == std::string::npos)
	{
		return "./";
	}

	// 截取最后一个'/'之前的部分
	std::string parentDir = path.substr(0, lastSlashPos);

	// 如果最后一个'/'是路径的最后一个字符，我们需要去掉它
	if (lastSlashPos == path.length() - 1)
	{
		// 再找前一个'/'，如果不存在，返回"./"，否则截取到那个'/'为止
		size_t secondLastSlashPos = path.rfind('/', lastSlashPos - 1);
		if (secondLastSlashPos == std::string::npos)
		{
			return "./";
		}
		else
		{
			parentDir = path.substr(0, secondLastSlashPos);
		}
	}

	// 添加斜杠以确保返回的上级目录以斜杠结尾
	parentDir += "/";

	return parentDir;
}

void FtpWidget::init_memu()
{
	QMenu *menu = new QMenu;
	QAction *cdupAction = menu->addAction("返回上级");
	QAction *refAction = menu->addAction("刷新");
	menu->addSeparator();
	QAction *putAction = menu->addAction("上传");
	QAction *getAction = menu->addAction("下载");
	menu->addSeparator();
	QAction *delAction = menu->addAction("删除");	// TODO
	QAction *renameAction = menu->addAction("重命名");
	menu->addSeparator();
	QAction *mkdirAction = menu->addAction("新建文件夹");
	// 鼠标右键发出 customContextMenuRequested 信号
	ui->tableWidget->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui->tableWidget, &QTableWidget::customContextMenuRequested, [=](const QPoint &pos)
	{
		// 打印鼠标在列表的哪一行
		qDebug() << ui->tableWidget->currentRow();
		// 菜单在右键的地方显示
		// viewport 返回局部坐标 mapToGlobal 局部坐标转全局坐标
		menu->exec(ui->tableWidget->viewport()->mapToGlobal(pos));
	});

	connect(cdupAction, &QAction::triggered, this, &FtpWidget::on_pushButton_cdup);
	connect(refAction, &QAction::triggered, this, &FtpWidget::on_pushButton_refresh);
	connect(putAction, &QAction::triggered, this, &FtpWidget::on_putAction);
	connect(getAction, &QAction::triggered, this, &FtpWidget::on_getAction);
}

void FtpWidget::on_pushButton_start()
{
	std::string ip = ui->lineEdit_ip->text().toStdString();
	int port = ui->lineEdit_port->text().toInt();
	std::string user = ui->lineEdit_user->text().toStdString();
	std::string passwd = ui->lineEdit_passwd->text().toStdString();
	std::string connect_type = ui->comboBox_connect->currentText().toStdString();

	// 换到等待界面
	ui->stackedWidget->setCurrentIndex(1);

	// ftp://ip
	std::string url = connect_type + "://" + ip;
	*p_interrupt_login_ = false;
	auto f = [=]()
	{
		bool ret = ftp_core_->Connect(url, port, user, passwd);
		if (ret && !(*p_interrupt_login_))
			// 跳转到工作界面
			emit sig_connect_OK();
		else
			// 跳转回登录界面
			emit sig_connect_failed();
	};
	std::thread th(f);
	th.detach();
}

void FtpWidget::on_pushButton_cancel_login()
{
	*p_interrupt_login_ = true;
	// 跳转回登录界面
	ui->stackedWidget->setCurrentIndex(0);
}

void FtpWidget::on_pushButton_refresh()
{
	std::vector<file_info> file_infos = ftp_core_->GetDirList(this->remote_path_);
	file_list_clear();
	file_list_add(file_infos);
}

void FtpWidget::on_lineEdit_url_working()
{
	QString path = ui->lineEdit_url_working->text();
	this->remote_path_ = path.toStdString();
	on_pushButton_refresh();
}

void FtpWidget::on_pushButton_cdup()
{
	const std::string par_path = getParentDirectory(remote_path_);
	remote_path_ = par_path;
	ui->lineEdit_url_working->setText(remote_path_.c_str());
	on_pushButton_refresh();
}

void FtpWidget::on_putAction()
{
	QFileDialog file_dialog(this, "上传文件");
	// QFileDialog::FileMode::ExistingFile 只允许用户选择一个已经存在的文件，而不是创建一个新文件
	file_dialog.setFileMode(QFileDialog::FileMode::ExistingFile);
	// 没有点击确认按钮 关闭文件窗口
	if (file_dialog.exec() != QDialog::Accepted)
		return;
	QStringList files = file_dialog.selectedFiles();

	// 最多只能选择一个文件
	if (files.size() > 1) // TODO
	{
		QMessageBox::warning(this, "选择文件过多！", "最多选择一个文件");
		return;
	}
	qDebug() << files[0];

	const std::filesystem::path path(files[0].toStdString());
	std::string remote_file = remote_path_ + "/" + path.filename().string();
	std::string loc_file = files[0].toStdString();
	// 上传
	std::future<CURLcode> ret = ftp_core_->PutFile(remote_file, loc_file);
	put_rets_.emplace_back(remote_file, loc_file, std::move(ret));

	// 假如没有启动 上传返回值检测线程 则启动线程
	if (is_putting_atomic_.load() == false)
	{
		is_putting_atomic_.store(true);
		check_put_result_thread_ = std::thread(&FtpWidget::check_put_result_thread_func_, this);
		check_put_result_thread_.detach();
	}
}

void FtpWidget::on_tableWidget_cellDoubleClicked(int row, int idx)
{
	// 位置不合理 退出
	if (row == -1)
		return;

	/*
	 * "名称", "大小", "修改日期", "权限", "用户", "用户组", "链接数"
	 *  [0]    [1]    [2]        [3]    [4]    [5]      [6]
	 */
	// 名称
	QString name = ui->tableWidget->item(row, 0)->text();
	// 假如这是个软链接
	if (ui->tableWidget->item(row, 3)->text()[0] == 'l')
	{
		/*
		 * bin -> usr/bin
		 */
		name = name.split("->")[0];
		name = name.trimmed(); // 去除首尾空格
	}
	// 如果是当前目录，那就不用跳
	if (name.trimmed() == ".")
		return;

	// 如果是跳转到上级
	if (name.trimmed() == "..")
		remote_path_ = getParentDirectory(remote_path_);
	else
		remote_path_ = remote_path_ +
		               (remote_path_[remote_path_.size() - 1] == '/' ? "" : "/") +
		               name.toStdString();


	on_pushButton_refresh();
	ui->lineEdit_url_working->setText(remote_path_.c_str());
}

void FtpWidget::on_getAction()
{
	/*
	 * "名称", "大小", "修改日期", "权限", "用户", "用户组", "链接数"
     *  [0]    [1]    [2]        [3]    [4]    [5]      [6]
     */

	// 鼠标点到了哪一行
	int row = ui->tableWidget->currentRow();
	if (row == -1)
	{
		QMessageBox::warning(this, "没有选择下载的文件", "没有选择下载文件");
		return;
	}

	QFileDialog file_dialog(this, "下载文件到");
	// QFileDialog::FileMode::AnyFile 允许用户选择任何类型的文件，包括新文件和现有文件
	file_dialog.setFileMode(QFileDialog::FileMode::AnyFile);

	// 文件名称
	QString name = ui->tableWidget->item(row, 0)->text();
	// 假如这是个软链接
	if (ui->tableWidget->item(row, 3)->text()[0] == 'l')
	{
		/*
		 * bin -> usr/bin
		 */
		name = name.split("->")[0];
		name = name.trimmed(); // 去除首尾空格
	}

	// 这是个文件夹
	if (ui->tableWidget->item(row, 3)->text()[0] == 'd')
	{
		QMessageBox::warning(this, "请重新选择", "这是个文件夹，无法下载");
		return;
	}

	// 记录列表的文件夹名
	file_dialog.selectFile(name);

	// 弹出文件框框 挑选下载路径
	if (file_dialog.exec() != QDialog::Accepted)
		return;

	QStringList files = file_dialog.selectedFiles();
	// 最多只能选择一个文件来下载
	if (files.size() > 1) // TODO
	{
		QMessageBox::warning(this, "选择文件过多！", "最多选择一个文件");
		return;
	}
	qDebug() << files[0];
	// 下载
	std::string remote_file = remote_path_ + "/" + name.toStdString();
	std::string loc_file = files[0].toStdString();
	std::future<CURLcode> ret = ftp_core_->GetFile(remote_file, loc_file);

	get_rets_.emplace_back(remote_file, loc_file, std::move(ret));

	// 假如没有启动 上传返回值检测线程 则启动线程
	if (is_getting_atomic_.load() == false)
	{
		is_getting_atomic_.store(true);
		check_get_result_thread_ = std::thread(&FtpWidget::check_get_result_thread_func_, this);
		check_get_result_thread_.detach();
	}
}

void FtpWidget::check_put_result_thread_func_()
{
	/**
	 * put_rets_
	 * 第1个 std::string 远程文件
	 * 第2个 std::string 本地文件
	 * 第3个 std::future<CURLcode> 上传完文件后的结果
	 */

	int i = 0;
	while (!put_rets_.empty())
	{
		if (put_rets_.size() > i)
		{
			// 假如已经上传完毕 即可查看结果
			// std::future<CURLcode>
			if (std::get<2>(put_rets_[i]).wait_for(std::chrono::milliseconds(100)) ==
			    std::future_status::ready)
			{
				// 远程文件
				QString remote_file = std::get<0>(put_rets_[i]).c_str();
				// 本地文件
				QString loc_file = std::get<1>(put_rets_[i]).c_str();
				// 上传的结果

				std::string msg{
					std::get<2>(put_rets_[i]).get() == CURLE_OK
						? "上传成功"
						: "上传失败"
				};
				QString curl_msg;
				curl_msg += "远程文件: " + remote_file + "\n本地文件: " + loc_file + "\n" + msg.c_str();

				emit sig_put_result(curl_msg);

				put_rets_.erase(put_rets_.begin() + i);
				continue;
			}
			else
				++i;
		}
		else
			i = 0;
	}

	is_putting_atomic_.store(false);
}

void FtpWidget::check_get_result_thread_func_()
{
	/**
	 * get_rets_
	 * 第1个 std::string 远程文件
	 * 第2个 std::string 本地文件
	 * 第3个 std::future<CURLcode> 下载完文件后的结果
	 */

	int i = 0;
	while (!get_rets_.empty())
	{
		if (get_rets_.size() > i)
		{
			// 假如已经下载完毕 即可查看结果
			// std::future<CURLcode>
			if (std::get<2>(get_rets_[i]).wait_for(std::chrono::milliseconds(100)) ==
			    std::future_status::ready)
			{
				// 远程文件
				QString remote_file = std::get<0>(get_rets_[i]).c_str();
				// 本地文件
				QString loc_file = std::get<1>(get_rets_[i]).c_str();
				// 上传的结果
				std::string msg{
					std::get<2>(get_rets_[i]).get() == CURLE_OK
						? "下载成功"
						: "下载失败"
				};
				QString curl_msg;
				curl_msg += "远程文件: " + remote_file + "\n本地文件: " + loc_file + "\n" + msg.c_str();

				emit sig_get_result(curl_msg);
				get_rets_.erase(get_rets_.begin() + i);
				continue;
			}
			else
				++i;
		}
		else
			i = 0;
	}

	is_getting_atomic_.store(false);
}
