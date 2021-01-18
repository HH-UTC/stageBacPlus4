#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFlags>
#include <QCloseEvent>
#include <QDebug>
#include <QHostAddress>
#include <QIODevice>
#include <QAbstractSocket>

/**
 * @brief MainWindow::MainWindow
 * @param parent
 */
MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    _socket(this)
{
    ui->setupUi(this);
    setupScatterPixmapDemo(ui->customPlot);

    // Initialisation
    init();


    // Connexion ou déconnecion IHM / Presto
    connect(this, SIGNAL(receptionStarted()), m_Reception, SLOT(connexion()));
    connect(this, SIGNAL(receptiondisconnected()), m_Reception, SLOT(disconnect()));

    // Envoie des paquets de message PF Com au Thread myHMI
    connect(m_Reception, SIGNAL(sendPFcomData(list<PlatformComMessage>)), myHMI, SLOT(readMessage(list<PlatformComMessage>)));

    // Reception d'une liste de 32 objet max de type Data par la view, update
    connect(myHMI, SIGNAL(readfinishCPU(std::list<Data>)), this, SLOT(readCpuData(std::list<Data>)));
    // Reception de la liste d'objet Memory par la view pour update
    connect(myHMI, SIGNAL(readfinishMemory(std::list<MemoryData>)), this, SLOT(readMemoryData(std::list<MemoryData>)));

    // envoie du fichier de conf Memory File
    connect(this, SIGNAL(confPath(QString)), myHMI, SLOT(copyConfPath(QString)));

    }

/**
 * @brief MainWindow::~MainWindow
 */
MainWindow::~MainWindow()
{
    myHMI->quit();
    myHMI->wait();

    m_Reception->quit();
    m_Reception->wait();
    delete ui;
}

MainWindow* MainWindow::myObject = nullptr;

/**
 * @brief MainWindow::init
 */
void MainWindow::init()
{
    // Création et lancement des threads
    myHMI = new HMI(this);
    myHMI->start();

    m_Reception = new Reception(this);
    m_Reception->start();

    // Initialisation du tri des heap / allocateur
    ui->treeWidget->sortItems(0, Qt::AscendingOrder);
    ui->treeAllocator->sortItems(0, Qt::AscendingOrder);

    // Init de l'indicateur de connexion
    ui->label_44->setStyleSheet("background: red");

    // Démarrage du timer pour actualiser le uptime et la moyenne global CPU
    connect(&refreshTimer, SIGNAL(timeout()), this, SLOT(updateGlobalCPUMax()));
    connect(&refreshTimer, SIGNAL(timeout()), this, SLOT(updateUpTime()));


    // Init des informations de connexion et du fichier platform
    connect(this, SIGNAL(IPchange(const QString)), m_Reception, SLOT(updateIPvalue(const QString)));
    connect(this, SIGNAL(Portchange(const QString)), m_Reception, SLOT(updatePortvalue(const QString)));
    connect(this, SIGNAL(opus3signal(QString)), myHMI, SLOT(getOpus3Platform(QString)));

    emit IPchange(ui->lineIP->text());
    emit Portchange(ui->linePort->text());
    emit opus3signal(QCoreApplication::applicationDirPath() + "/opus3/opus3.xml");

    // refresh du nombre de messages reçus par l'IHM
    connect(m_Reception, SIGNAL(sendNbMsgToView(int)), this, SLOT(updateNbMessageReceived(int)));
}

/**********************************************************************************
// test lié à la mémoire
**********************************************************************************/

void MainWindow::on_reset_clicked() // Reset
{
    // On reset les arbres Memory
    ui->treeWidget->clear();
    ui->treeAllocator->clear();

}

/**
 * @brief MainWindow::addHeap
 * @param node
 * @param name
 * @param maxUsedPhysicalPlacePercent
 * @param maxUsedPhysicalPlace
 * @param maxUsedVirtualPlacePercent
 * @param maxUsedVirtualPlace
 */
void MainWindow::addHeap(QString node, string name, int maxUsedPhysicalPlacePercent, int maxUsedPhysicalPlace, QString maxUsedVirtualPlacePercent, QString maxUsedVirtualPlace)
{
    // Création d'un treeItem
    QTreeWidgetItem *treeItem = new QTreeWidgetItem(ui->treeWidget);

    // Initialisation de chaque colonne
    // On masque les potentielles redondances
    if (name != "MEMORY_LOCALPHYSICAL" && name != "MEMORY_LOCAL" && name != "")
    {
        if (node == "--") treeItem->setText(0, node);
        else treeItem->setData(0, Qt::EditRole, node.toInt());

        treeItem->setText(1, QString::fromStdString(name));
        treeItem->setData(2, Qt::EditRole, maxUsedPhysicalPlacePercent);
        treeItem->setData(3, Qt::EditRole, maxUsedPhysicalPlace);
        if (node == "--") treeItem->setText(4, maxUsedVirtualPlacePercent);
        else treeItem->setData(4, Qt::EditRole, maxUsedVirtualPlacePercent.toInt());
        if (node == "--") treeItem->setText(5, maxUsedVirtualPlace);
        else treeItem->setData(5, Qt::EditRole, maxUsedVirtualPlace.toInt());

        // Si global heap, mettre une couleur
        if (name != "LOCAL")
        {
            treeItem->setBackgroundColor(0, Qt::lightGray);
            treeItem->setBackgroundColor(1, Qt::lightGray);
            treeItem->setBackgroundColor(2, Qt::lightGray);
            treeItem->setBackgroundColor(3, Qt::lightGray);
            treeItem->setBackgroundColor(4, Qt::lightGray);
            treeItem->setBackgroundColor(5, Qt::lightGray);
        }
    }
}

/**
 * @brief MainWindow::addAllocator
 * @param nomParent
 * @param node
 * @param id
 * @param name
 * @param occupationpercent
 * @param occupation
 */
void MainWindow::addAllocator(int node, string name, string id, string nomParent, int allocationpercent, int size)
{
    // Création d'un treeItem
    QTreeWidgetItem *treeItem = new QTreeWidgetItem(ui->treeAllocator);
    // Initialisation
    treeItem->setData(0, Qt::EditRole, node);

    if (name != "--") treeItem->setText(1, QString::fromStdString(name));
    else treeItem->setText(1, "0x" + QString::fromStdString(id));

    treeItem->setText(2, QString::fromStdString(nomParent));
    treeItem->setData(3, Qt::EditRole, allocationpercent);
    treeItem->setData(4, Qt::EditRole, size);
}

/**
 * @brief MainWindow::readMemoryData
 * @param p_listMemoryData
 */
void MainWindow::readMemoryData(std::list<MemoryData> p_listMemoryData)
{
    // On clear l'arbre
    ui->treeWidget->clear();
    ui->treeAllocator->clear();
    int i = 0;
    // On parcourt les données
    for (std::list<MemoryData>::iterator it = p_listMemoryData.begin(); it != p_listMemoryData.end(); it++)
    {
        MemoryData elt = *it;
        // On commence par aficher le noeud puis ses allocateurs s'il en existe
        addHeap(elt.getNode(), elt.getMemoryName(), elt.getmaxUsedPhysicalPlacePercent().toInt(), elt.getmaxUsedPhysicalPlace().toInt(), elt.getmaxUsedVirtualPlacePercent(), elt.getmaxUsedVirtualPlace());
        if (elt.getAllocList().size() != 0)
        {
            std::list<AllocData> l_listAlloc = elt.getAllocList();
            // on parcourt la liste des allocateurs
            for (std::list<AllocData>::iterator it2 = l_listAlloc.begin(); it2 != l_listAlloc.end(); it2++)
            {
                i++;
                AllocData currentAlloc = *it2;
                int maxUsedAllocMem = currentAlloc.getMaxUsedMem();
                int sizeAllocMem = currentAlloc.getTotalMem();
                // On les affiche sur l'arbre
                addAllocator(currentAlloc.getNode(), currentAlloc.getAllocatorName(), currentAlloc.getId(), currentAlloc.getParentName(), maxUsedAllocMem, sizeAllocMem);

            }
        }
    }
    // Dimensionnement de la largeur de certaines colonnes
    ui->treeWidget->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    ui->treeAllocator->header()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
}


/**********************************************************************************
 * Partie CPU
 *
 * Reset des max
**********************************************************************************/

/**
 * @brief MainWindow::on_Reset_max_clicked
 */
void MainWindow::on_Reset_max_clicked()
{
    ui->maxnode0->setText("0");
    ui->maxnode1->setText("0");
    ui->maxnode2->setText("0");
    ui->maxnode3->setText("0");
    ui->maxnode4->setText("0");
    ui->maxnode5->setText("0");
    ui->maxnode6->setText("0");
    ui->maxnode7->setText("0");
    ui->maxnode8->setText("0");
    ui->maxnode9->setText("0");
    ui->maxnode10->setText("0");
    ui->maxnode11->setText("0");
    ui->maxnode12->setText("0");
    ui->maxnode13->setText("0");
    ui->maxnode14->setText("0");
    ui->maxnode15->setText("0");
    ui->maxnode16->setText("0");
    ui->maxnode17->setText("0");
    ui->maxnode18->setText("0");
    ui->maxnode19->setText("0");
    ui->maxnode20->setText("0");
    ui->maxnode21->setText("0");
    ui->maxnode22->setText("0");
    ui->maxnode23->setText("0");
    ui->maxnode24->setText("0");
    ui->maxnode25->setText("0");
    ui->maxnode26->setText("0");
    ui->maxnode27->setText("0");
    ui->maxnode28->setText("0");
    ui->maxnode29->setText("0");
    ui->maxnode30->setText("0");
    ui->maxnode31->setText("0");


}


/**********************************************************************************
// Lecture des valeurs CPU
**********************************************************************************/

/**
 * @brief MainWindow::readCpuData
 * @param p_listeData
 */
void MainWindow::readCpuData(std::list<Data> p_listeData)
{
    // On parcours la liste des éléments de type Data
    for (std::list<Data>::iterator it = p_listeData.begin(); it != p_listeData.end(); it++)
    {
        Data elt = *it;
        queue<int> newestFifo = elt.getFifo().back();
        int newestValue = newestFifo.back();
        int newestMaxValue = elt.maxCPUvalue(newestFifo);
        // On met à jour la valeur CPU graphiquement selon l'id donné
        switch (elt.getId())
        {
        case 0 :
            if (newestMaxValue > (ui->maxnode0->text()).toInt()) ui->maxnode0->setText(QString::number(newestMaxValue));
            ui->node0Bar->setValue(newestValue);
            break;
        case 1 :
            if (newestMaxValue > (ui->maxnode1->text()).toInt()) ui->maxnode1->setText(QString::number(newestMaxValue));
            ui->node1Bar->setValue(newestValue);
            break;
        case 2 :
            if (newestMaxValue > (ui->maxnode2->text()).toInt()) ui->maxnode2->setText(QString::number(newestMaxValue));
            ui->node2Bar->setValue(newestValue);
            break;
        case 3 :
            if (newestMaxValue > (ui->maxnode3->text()).toInt()) ui->maxnode3->setText(QString::number(newestMaxValue));
            ui->node3Bar->setValue(newestValue);
            break;
        case 4 :
            if (newestMaxValue > (ui->maxnode4->text()).toInt()) ui->maxnode4->setText(QString::number(newestMaxValue));
            ui->node4Bar->setValue(newestValue);
            break;
        case 5 :
            if (newestMaxValue > (ui->maxnode5->text()).toInt()) ui->maxnode5->setText(QString::number(newestMaxValue));
            ui->node5Bar->setValue(newestValue);
            break;
        case 6 :
            if (newestMaxValue > (ui->maxnode6->text()).toInt()) ui->maxnode6->setText(QString::number(newestMaxValue));
            ui->node6Bar->setValue(newestValue);
            break;
        case 7 :
            if (newestMaxValue > (ui->maxnode7->text()).toInt()) ui->maxnode7->setText(QString::number(newestMaxValue));
            ui->node7Bar->setValue(newestValue);
            break;
        case 8 :
            if (newestMaxValue > (ui->maxnode8->text()).toInt()) ui->maxnode8->setText(QString::number(newestMaxValue));
            ui->node8Bar->setValue(newestValue);
            break;
        case 9 :
            if (newestMaxValue > (ui->maxnode9->text()).toInt()) ui->maxnode9->setText(QString::number(newestMaxValue));
            ui->node9Bar->setValue(newestValue);
            break;
        case 10 :
            if (newestMaxValue > (ui->maxnode10->text()).toInt()) ui->maxnode10->setText(QString::number(newestMaxValue));
            ui->node10Bar->setValue(newestValue);
            break;
        case 11 :
            if (newestMaxValue > (ui->maxnode11->text()).toInt()) ui->maxnode11->setText(QString::number(newestMaxValue));
            ui->node11Bar->setValue(newestValue);
            break;
        case 12 :
            if (newestMaxValue > (ui->maxnode12->text()).toInt()) ui->maxnode12->setText(QString::number(newestMaxValue));
            ui->node12Bar->setValue(newestValue);
            break;
        case 13 :
            if (newestMaxValue > (ui->maxnode13->text()).toInt()) ui->maxnode13->setText(QString::number(newestMaxValue));
            ui->node13Bar->setValue(newestValue);
            break;
        case 14 :
            if (newestMaxValue > (ui->maxnode14->text()).toInt()) ui->maxnode14->setText(QString::number(newestMaxValue));
            ui->node14Bar->setValue(newestValue);
            break;
        case 15 :
            if (newestMaxValue > (ui->maxnode15->text()).toInt()) ui->maxnode15->setText(QString::number(newestMaxValue));
            ui->node15Bar->setValue(newestValue);
            break;
        case 16 :
            if (newestMaxValue > (ui->maxnode16->text()).toInt()) ui->maxnode16->setText(QString::number(newestMaxValue));
            ui->node16Bar->setValue(newestValue);
            break;
        case 17 :
            if (newestMaxValue > (ui->maxnode17->text()).toInt()) ui->maxnode17->setText(QString::number(newestMaxValue));
            ui->node17Bar->setValue(newestValue);
            break;
        case 18 :
            if (newestMaxValue > (ui->maxnode18->text()).toInt()) ui->maxnode18->setText(QString::number(newestMaxValue));
            ui->node18Bar->setValue(newestValue);
            break;
        case 19 :
            if (newestMaxValue > (ui->maxnode19->text()).toInt()) ui->maxnode19->setText(QString::number(newestMaxValue));
            ui->node19Bar->setValue(newestValue);
            break;
        case 20 :
            if (newestMaxValue > (ui->maxnode20->text()).toInt()) ui->maxnode20->setText(QString::number(newestMaxValue));
            ui->node20Bar->setValue(newestValue);
            break;
        case 21 :
            if (newestMaxValue > (ui->maxnode21->text()).toInt()) ui->maxnode21->setText(QString::number(newestMaxValue));
            ui->node21Bar->setValue(newestValue);
            break;
        case 22 :
            if (newestMaxValue > (ui->maxnode22->text()).toInt()) ui->maxnode22->setText(QString::number(newestMaxValue));
            ui->node22Bar->setValue(newestValue);
            break;
        case 23 :
            if (newestMaxValue > (ui->maxnode23->text()).toInt()) ui->maxnode23->setText(QString::number(newestMaxValue));
            ui->node23Bar->setValue(newestValue);
            break;
        case 24 :
            if (newestMaxValue > (ui->maxnode24->text()).toInt()) ui->maxnode24->setText(QString::number(newestMaxValue));
            ui->node24Bar->setValue(newestValue);
            break;
        case 25 :
            if (newestMaxValue > (ui->maxnode25->text()).toInt()) ui->maxnode25->setText(QString::number(newestMaxValue));
            ui->node25Bar->setValue(newestValue);
            break;
        case 26 :
            if (newestMaxValue > (ui->maxnode26->text()).toInt()) ui->maxnode26->setText(QString::number(newestMaxValue));
            ui->node26Bar->setValue(newestValue);
            break;
        case 27 :
            if (newestMaxValue > (ui->maxnode27->text()).toInt()) ui->maxnode27->setText(QString::number(newestMaxValue));
            ui->node27Bar->setValue(newestValue);
            break;
        case 28 :
            if (newestMaxValue > (ui->maxnode28->text()).toInt()) ui->maxnode28->setText(QString::number(newestMaxValue));
            ui->node28Bar->setValue(newestValue);
            break;
        case 29 :
            if (newestMaxValue > (ui->maxnode29->text()).toInt()) ui->maxnode29->setText(QString::number(newestMaxValue));
            ui->node29Bar->setValue(newestValue);
            break;
        case 30 :
            if (newestMaxValue > (ui->maxnode30->text()).toInt()) ui->maxnode30->setText(QString::number(newestMaxValue));
            ui->node30Bar->setValue(newestValue);
            break;
        case 31 :
            if (newestMaxValue > (ui->maxnode31->text()).toInt()) ui->maxnode31->setText(QString::number(newestMaxValue));
            ui->node31Bar->setValue(newestValue);
            break;
        default:
            break;
        }
    }
}


/**********************************************************************************
// Affichage des graphes
**********************************************************************************/

void MainWindow::on_ViewCluster0_clicked()
{
    plotwindow1 = new PlotWindow(0);
    connect(myHMI, SIGNAL(readfinishCPU(std::list<Data>)), plotwindow1, SLOT(copyFifoData(std::list<Data>)));
    plotwindow1->setWindowTitle("Cluster0");
    plotwindow1->show();
}

void MainWindow::on_ViewCluster1_clicked()
{
    plotwindow2 = new PlotWindow(4);
    connect(myHMI, SIGNAL(readfinishCPU(std::list<Data>)), plotwindow2, SLOT(copyFifoData(std::list<Data>)));
    plotwindow2->setWindowTitle("Cluster1");
    plotwindow2->show();
}

void MainWindow::on_ViewCluster2_clicked()
{
    plotwindow3 = new PlotWindow(8);
    connect(myHMI, SIGNAL(readfinishCPU(std::list<Data>)), plotwindow3, SLOT(copyFifoData(std::list<Data>)));
    plotwindow3->setWindowTitle("Cluster2");
    plotwindow3->show();
}

void MainWindow::on_ViewCluster3_clicked()
{
    plotwindow4 = new PlotWindow(12);
    connect(myHMI, SIGNAL(readfinishCPU(std::list<Data>)), plotwindow4, SLOT(copyFifoData(std::list<Data>)));
    plotwindow4->setWindowTitle("Cluster3");
    plotwindow4->show();
}

void MainWindow::on_ViewCluster4_clicked()
{
    plotwindow5 = new PlotWindow(16);
    connect(myHMI, SIGNAL(readfinishCPU(std::list<Data>)), plotwindow5, SLOT(copyFifoData(std::list<Data>)));
    plotwindow5->setWindowTitle("Cluster4");
    plotwindow5->show();
}

void MainWindow::on_ViewCluster5_clicked()
{
    plotwindow6 = new PlotWindow(20);
    connect(myHMI, SIGNAL(readfinishCPU(std::list<Data>)), plotwindow6, SLOT(copyFifoData(std::list<Data>)));
    plotwindow6->setWindowTitle("Cluster5");
    plotwindow6->show();
}

void MainWindow::on_ViewCluster6_clicked()
{
    plotwindow7 = new PlotWindow(24);
    connect(myHMI, SIGNAL(readfinishCPU(std::list<Data>)), plotwindow7, SLOT(copyFifoData(std::list<Data>)));
    plotwindow7->setWindowTitle("Cluster6");
    plotwindow7->show();
}

void MainWindow::on_ViewCluster7_clicked()
{
    plotwindow8 = new PlotWindow(28);
    connect(myHMI, SIGNAL(readfinishCPU(std::list<Data>)), plotwindow8, SLOT(copyFifoData(std::list<Data>)));
    plotwindow8->setWindowTitle("Cluster7");
    plotwindow8->show();
}

/**
 * @brief MainWindow::on_actionMemory_Configuration_triggered
 */
void MainWindow::on_actionMemory_Configuration_triggered()
{
    l_configLoad2 = QFileDialog::getOpenFileName(this, "Load", QString(), "Fichier Texte(*.txt)");
    if (isStart == 0)
    {
        emit confPath(l_configLoad2);
    }
}

/**
 * @brief MainWindow::on_actionStart_triggered
 */
void MainWindow::on_actionStart_triggered()
{
    emit receptionStarted();
    // IHM lancée
    isStart = 1;
    ui->label_44->setStyleSheet("background: green");
    refreshTimer.start(1000); // 1 seconde
}

/**
 * @brief MainWindow::on_actionStop_triggered
 */
void MainWindow::on_actionStop_triggered()
{
    emit receptiondisconnected();
    ui->label_44->setStyleSheet("background: red");
    refreshTimer.stop();

}

/**
 * @brief MainWindow::updateGlobalCPUMax
 */
void MainWindow::updateGlobalCPUMax()
{
    // Init
    int CPUvalues = 0;
    int nbCPU = 0;
    list<int> listCPUvalue;

    // On lit toutes les valeurs CPU courantes et on les push dans la liste
    int CPU0 = ui->node0Bar->value(); listCPUvalue.push_back(CPU0); int CPU1 = ui->node1Bar->value(); listCPUvalue.push_back(CPU1);
    int CPU2 = ui->node2Bar->value(); listCPUvalue.push_back(CPU2); int CPU3 = ui->node3Bar->value(); listCPUvalue.push_back(CPU3);
    int CPU4 = ui->node4Bar->value(); listCPUvalue.push_back(CPU4); int CPU5 = ui->node5Bar->value(); listCPUvalue.push_back(CPU5);
    int CPU6 = ui->node6Bar->value(); listCPUvalue.push_back(CPU6); int CPU7 = ui->node7Bar->value(); listCPUvalue.push_back(CPU7);
    int CPU8 = ui->node8Bar->value(); listCPUvalue.push_back(CPU8); int CPU9 = ui->node9Bar->value(); listCPUvalue.push_back(CPU9);
    int CPU10 = ui->node10Bar->value(); listCPUvalue.push_back(CPU10); int CPU11 = ui->node11Bar->value(); listCPUvalue.push_back(CPU11);
    int CPU12 = ui->node12Bar->value(); listCPUvalue.push_back(CPU12); int CPU13 = ui->node13Bar->value(); listCPUvalue.push_back(CPU13);
    int CPU14 = ui->node14Bar->value(); listCPUvalue.push_back(CPU14); int CPU15 = ui->node15Bar->value(); listCPUvalue.push_back(CPU15);
    int CPU16 = ui->node16Bar->value(); listCPUvalue.push_back(CPU16); int CPU17 = ui->node17Bar->value(); listCPUvalue.push_back(CPU17);
    int CPU18 = ui->node18Bar->value(); listCPUvalue.push_back(CPU18); int CPU19 = ui->node19Bar->value(); listCPUvalue.push_back(CPU19);
    int CPU20 = ui->node20Bar->value(); listCPUvalue.push_back(CPU20); int CPU21 = ui->node21Bar->value(); listCPUvalue.push_back(CPU21);
    int CPU22 = ui->node22Bar->value(); listCPUvalue.push_back(CPU22); int CPU23 = ui->node23Bar->value(); listCPUvalue.push_back(CPU23);
    int CPU24 = ui->node24Bar->value(); listCPUvalue.push_back(CPU24); int CPU25 = ui->node25Bar->value(); listCPUvalue.push_back(CPU25);
    int CPU26 = ui->node26Bar->value(); listCPUvalue.push_back(CPU26); int CPU27 = ui->node27Bar->value(); listCPUvalue.push_back(CPU27);
    int CPU28 = ui->node28Bar->value(); listCPUvalue.push_back(CPU28); int CPU29 = ui->node29Bar->value(); listCPUvalue.push_back(CPU29);
    int CPU30 = ui->node30Bar->value(); listCPUvalue.push_back(CPU30); int CPU31 = ui->node31Bar->value(); listCPUvalue.push_back(CPU31);

    for (std::list<int>::iterator it = listCPUvalue.begin(); it != listCPUvalue.end(); it++)
    {
        // Si les valeurs CPU sont > -1, cela veut dire que le coeur est actif et sera pris en compte pour le calcul de la moyenne global
        if (*it > -1)
        {
            CPUvalues += *it;
            nbCPU++;
        }
    }

    if (nbCPU !=0) // pour éviter division par 0
    {
        // On met à jouir la view et le graphe
        ui->overallCPUbar->setValue(CPUvalues/nbCPU);
        realtimeCPUaverageSlot((CPUvalues/nbCPU)/1.0,  m_timeElapsed);
        if (CPUvalues/nbCPU > (ui->MaxCPUaverage->text()).toInt()) ui->MaxCPUaverage->setText(QString::number(CPUvalues/nbCPU));
    }
    // Mise à jour nombre de coeurs utilisés
    ui->nbUsedNodes->setText(QString::number(nbCPU));
}

/**
 * @brief MainWindow::updateUpTime
 */
void MainWindow::updateUpTime()
{
    static QTime time(QTime::currentTime());
    // calculate two new data points:
    int rest = time.elapsed()/1000; // time elapsed since start of demo, in seconds //86370000
    m_timeElapsed = time.elapsed()/1000;

    int conv[4] = {86400,3600,60,1};
    int result[4] = {0,0,0,0};
    int i=0;

    // Conversion seconde en d:h:m:s
    while (rest>0)
    {
        result[i]= rest/conv[i];
        rest=rest-result[i]*conv[i];
        i+=1;
    }
    QString h, m, s;
    if (result[3] < 10)  s = "0" + QString::number(result[3]);
    else s = QString::number(result[3]);
    if (result[2] < 10)  m = "0" + QString::number(result[2]);
    else m = QString::number(result[2]);
    if (result[1] < 10)  h = "0" + QString::number(result[1]);
    else h = QString::number(result[1]);

    // Affichage de l'up time
    ui->upTimeLabel->setText(QString::number(result[0]) + ":" + h + ":" + m + ":" + s);

}

/**
 * @brief MainWindow::updateNbMessageReceived
 * @param p_nbMessageCounter
 */
void MainWindow::updateNbMessageReceived(int p_nbMessageCounter)
{
    ui->nbMessage->setText(QString::number(p_nbMessageCounter));
}

/**
 * @brief MainWindow::setupScatterPixmapDemo
 * @param customPlot
 */
void MainWindow::setupScatterPixmapDemo(QCustomPlot *customPlot)
{
  // Code relatif au graphe
  customPlot->addGraph();
  customPlot->graph()->setLineStyle(QCPGraph::lsLine);
  QPen pen;
  pen.setColor(QColor(98, 169, 210));
  pen.setStyle(Qt::DashLine);
  pen.setWidthF(2.5);
  customPlot->graph()->setPen(pen);
  customPlot->graph()->setBrush(QBrush(QColor(203, 232, 246,70)));

  QSharedPointer<QCPAxisTickerTime> timeTicker(new QCPAxisTickerTime);
  customPlot->xAxis->setTicker(timeTicker);
  timeTicker->setTimeFormat("Day %d\n%h:%m:%s");
  customPlot->axisRect()->setupFullAxesBox();
  customPlot->yAxis->setRange(0, 100);
  customPlot->xAxis2->setVisible(true);
  customPlot->yAxis2->setVisible(true);
  customPlot->xAxis2->setTickLabels(false);
  customPlot->yAxis2->setTickLabels(false);
  customPlot->xAxis2->setTicks(false);
  customPlot->yAxis2->setTicks(false);
  customPlot->xAxis2->setSubTicks(false);
  customPlot->yAxis2->setSubTicks(false);

  customPlot->xAxis->setTicks(false);
  customPlot->yAxis->setTicks(false);

  connect(customPlot->xAxis, SIGNAL(rangeChanged(QCPRange)), customPlot->xAxis2, SLOT(setRange(QCPRange)));
  connect(customPlot->yAxis, SIGNAL(rangeChanged(QCPRange)), customPlot->yAxis2, SLOT(setRange(QCPRange)));

}

/**
 * @brief MainWindow::realtimeCPUaverageSlot
 * @param p_cpuM
 * @param p_key
 */
void MainWindow::realtimeCPUaverageSlot(double p_cpuM, double p_key)
{
    // add data to lines
    ui->customPlot->graph()->addData(p_key, p_cpuM);

    // On modifie l'échelle ici, fixé à 60 secondes
    ui->customPlot->xAxis->setRange(p_key, 60, Qt::AlignRight);
    ui->customPlot->replot();
}

/**
 * @brief MainWindow::on_actionConnexion_Setting_triggered
 */
void MainWindow::on_actionConnexion_Setting_triggered()
{
    // Ouverture de la fenêtre connexion setting
    connexionDialog = new ConnexionSetting();
    connect(connexionDialog, SIGNAL(connexionInfo(QString, unsigned short)), m_Reception, SLOT(updateConnexionvalue(QString, unsigned short)));
    connect(connexionDialog, SIGNAL(connexionInfo(QString, unsigned short)), this, SLOT(updateConnexionView(QString, unsigned short)));
    connexionDialog->setWindowTitle("Set connexion");
    connexionDialog->show();
}

/**
 * @brief MainWindow::updateConnexionView
 * @param p_ipValue
 * @param p_portValue
 */
void MainWindow::updateConnexionView(QString p_ipValue, unsigned short p_portValue)
{
    ui->lineIP->setText(p_ipValue);
    ui->linePort->setText(QString::number(p_portValue));
}

/**
 * @brief MainWindow::on_actionOpus3_triggered
 */
void MainWindow::on_actionOpus3_triggered()
{
    // Si Opus3 choisi
    if (ui->actionOpus3->isChecked() == true)
    {
        ui->actionOpus2->setChecked(false);
        QString  l_platformDir = QCoreApplication::applicationDirPath() + "/opus3/opus3.xml";
        emit opus3signal(l_platformDir);
    }

}

/**
 * @brief MainWindow::on_actionOpus2_triggered
 */
void MainWindow::on_actionOpus2_triggered()
{
    if (ui->actionOpus2->isChecked() == true)
    {
        ui->actionOpus3->setChecked(false);
        QString  l_platformDir = QCoreApplication::applicationDirPath() + "/opus2/opus2.xml";
        emit opus2signal(l_platformDir);
        connect(connexionDialog, SIGNAL(opus2signal(QString)), myHMI, SLOT(getOpus2Platform(QString)));
    }
}
