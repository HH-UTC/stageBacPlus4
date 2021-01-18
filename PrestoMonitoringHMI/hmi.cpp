#include "hmi.h"
#include <QDebug>
#include <QMessageBox>
#include <QFile>

/**
 * @brief HMI::HMI
 * @param parent
 */
HMI::HMI(QObject *parent) :
    QThread(parent)
{
    // On initialise la liste d'objet de type Data(id, first elt of FIFO, isUse)
    for (int i = 0; i < 32; i++)
    {
        Data obj = Data(i, -1, 0, 0);
        m_listeData.push_back(obj);
    }
}

HMI* HMI::myHMI = nullptr;

/**
 * @brief HMI::readMessage
 * @param p_PFComList
 */
void HMI::readMessage(list<PlatformComMessage> p_PFComList)
{
    // Nombre de message PF Com à traiter
    int nbPFcomMSG = p_PFComList.size();
    int nb = 0;
    // On parcours la liste
    while (nb < nbPFcomMSG)
    {
        PlatformComMessage elt = p_PFComList.front();
        // On récupère le message et sa size
        int messageSize = elt.getDatagramSize();
        string message = elt.getDatagram();

        int idCursor = 0;

        while (idCursor < messageSize)
        {
            // Début de la lecture, booléen isFront à true, la première info de monitoring à lire est en tête du message (juste après le header), on applique un offset car le header ne nous intéresse pas
            bool isFront = true;
            int nodeID = (int) message[idCursor];
            // Bite de type
            unsigned b = (unsigned)message[idCursor + 1];


            // On regarde si le bit CPU est levé
            if ((b & 0x01) != 0)
            {
                // traitement CPU
                readCPUData(idCursor, message);

                idCursor += 11;
                isFront = false;
            }

            // On regarde si le bit Allocator est levé
            if ((b & 0x02) != 0)
            {
                if (isFront == true)
                {
                    // offset de 10 octets
                    // traitement Allocator
                    readAllocatorData(nodeID, idCursor + 10, message);

                    isFront = false;
                    idCursor += ((int) message[idCursor + 10]*3*4) + 11;
                }
                else
                {
                    // traitement Allocator
                    readAllocatorData(nodeID, idCursor, message);

                    idCursor += ((int) message[idCursor]*3*4) + 1;
                    //qDebug() << idCursor;
                }

            }

            // On regarde si le bit Heap local est levé
            if ((b & 0x04) != 0)
            {
                if (isFront == true)
                {
                    // offset de 10 octets
                    // traitement Heap local
                    readHeapLocalData(nodeID, idCursor + 10, message);

                    isFront = false;
                    idCursor += 4*4 + 10;
                }
                else
                {
                    // traitement Heap local
                    readHeapLocalData(nodeID, idCursor, message);

                    idCursor += 4*4;
                    //qDebug() << idCursor;
                }
            }

            // On regarde si le bit Heap memories est levé
            if ((b & 0x08) != 0)
            {
                if (isFront == true)
                {
                    // offset de 10 octets
                    // traitement Heap
                    readHeapMemoryData(idCursor + 10, message);

                    isFront = false;
                    idCursor += ((int) message[idCursor + 10]*(2*4 + 1)) + 11;
                }
                else
                {
                    // traitement Heap
                    readHeapMemoryData(idCursor, message);

                    idCursor += ((int) message[idCursor]*(2*4 + 1)) + 1;
                    //qDebug() << idCursor;
                }
            }
        }
        if (p_PFComList.size() !=0) p_PFComList.pop_front();
        nb++;

    }

    // Enfin, on reset la variable m_isUse pour le prochain cycle
    for (unsigned int j = 0; j < m_listeData.size(); j++)
    {
        Data elt = m_listeData.front();
        if (elt.getIsUse() == 1)
        {
            elt.setIsUse(0);
        }
        m_listeData.push_back(elt);
        m_listeData.pop_front();
    }

    // Emission des signaux contenant les données vers la vue
    emit readfinishCPU(m_listeData);
    emit readfinishMemory(m_listMemoryData);
}

/**
 * @brief HMI::~HMI
 */
HMI::~HMI()
{
    delete myHMI;
}

/**
 * @brief HMI::readCPUData
 * @param p_cursor
 * @param message
 */
void HMI::readCPUData(int p_cursor, string message)
{
    // On actualise la structure de données CPU
    unsigned int i = 0;
    // On parcourt la liste des coeurs
    while (i < m_listeData.size())
    {
        Data elt = m_listeData.front();
        // Si on tombe sur le coeur à actualiser
        if (elt.getId() == (int)message[p_cursor])
        {
            // On push la nouvelle valeur CPU dans la fifo
            elt.feadFifo(elt.getIsUse(), (int)message[p_cursor+10]);

            // Calcul du time stamp en milliseconde puis push
            int seconde = qToBigEndian(*((int *)&message[p_cursor+2]));
            int nanoseconde = qToBigEndian(*((int *)&message[p_cursor+6]));
            int milliseconde = seconde * 1000 + nanoseconde/1000000;
            elt.addTS(elt.getIsUse(), milliseconde);

            // le coeur à cette id est actif, on modifie la valeur de m_isUse
            elt.setIsUse(1);

            m_listeData.push_back(elt);
            m_listeData.pop_front();
            break;
        }
        else
        {
            m_listeData.push_back(elt);
            m_listeData.pop_front();
        }
        i++;
    }
}

/**
 * @brief HMI::readAllocatorData
 * @param nodeID
 * @param p_cursor
 * @param message
 */
void HMI::readAllocatorData(int nodeID, int p_cursor, std::string message)
{
    // On actualise la structure de données Mémoire : Allocateur

    // On parcours la partie Allocateur du message Platform Com, le premier octet correspond au nombre d'Allocateur
    int initialPos = p_cursor;
    for (int k = 0; k < (int)message[initialPos]; k++)
    {
        // étiquette de l'allocateur que l'on souhaite ajouter
        // Conversion int to string, buffer initialiser
        char buff[33];
        // On récupère le nom du noeud
        string AllocMem = AllocatorIDtoAttachedMemory(itoa(qToBigEndian(*((int *)&message[p_cursor + 1])), buff, 16));

        unsigned int j = 0;
        // existance du noeud
        bool nodeexist = false;
        // existance de l'allocateur dans le noeud
        bool exist = false;

        // On parcours la structure de données Mémoire de noeud composé d'allocateur
        while (j < m_listMemoryData.size())
        {
            // On lit le premier noeud et on récupère la liste d'allocateur
            MemoryData elt = m_listMemoryData.front();
            std::list<AllocData> listAllocData = elt.getAllocList();

            // Si le nom du nom de l'allocateur à actualiser/ajouter existe dans la structure de données
            if (AllocMem == elt.getMemoryName())
            {
                // Alors le noeud existe
                nodeexist = true;

                // On parcours la liste des allocateurs du noeud
                unsigned int i = 0;
                while (i < listAllocData.size())
                {
                    // On crée une copie
                    AllocData elt2 = listAllocData.front();
                    char buff[33];
                    // Si l'allocateur existe déjà dans la liste, on l'actualise
                    if (elt2.getId() == itoa(qToBigEndian(*((int *)&message[p_cursor + 1])), buff, 16) && elt2.getNode() == nodeID)

                    {
                        // Actualisation des valeurs
                        elt2.updateMaxValue(qToBigEndian(*((int *)&message[p_cursor + 5])));
                        elt2.setTotalMemory(qToBigEndian(*((int *)&message[p_cursor + 9])));
                        listAllocData.push_back(elt2);
                        listAllocData.pop_front();

                        // actualise la liste d'allocateur copié
                        elt.setAllocList(listAllocData);

                        // on met à jour la liste mère
                        m_listMemoryData.push_back(elt);
                        m_listMemoryData.pop_front();
                        exist = true;
                        break;
                    }
                    // Sinon, on continue à parcourir la liste d'allocateur
                    else
                    {
                        listAllocData.push_back(elt2);
                        listAllocData.pop_front();
                    }
                    i++;
                }
            }

            // Sinon, on continue à parcourir la liste de noeud
            else
            {
                m_listMemoryData.push_back(elt);
                m_listMemoryData.pop_front();
            }
            j++;
        }

        // Si le noeud n'existe pas, on le crée
        if (nodeexist == false)
        {
            MemoryData newNode;
            newNode.setName(AllocMem);

            // Initialisation
            AllocData newAlloc;
            char buff[33];
            newAlloc.setAllocatorNode(nodeID);
            newAlloc.setAllocatorID(itoa(qToBigEndian(*((int *)&message[p_cursor + 1])), buff, 16));

            newAlloc.updateMaxValue(qToBigEndian(*((int *)&message[p_cursor + 5])));
            newAlloc.setTotalMemory(qToBigEndian(*((int *)&message[p_cursor + 9])));

            newAlloc.setAllocatorName(AllocatorIDtoName(newAlloc.getId()));
            newAlloc.setAllocatorParent(AllocatorIDtoAttachedMemory(newAlloc.getId()));

            std::list<AllocData> newAllocList;
            newAllocList.push_back(newAlloc);
            newNode.setAllocList(newAllocList);

            m_listMemoryData.push_back(newNode);
        }

        // Si le noeud exist mais pas l'allocateur
        if (exist == false && nodeexist == true)
        {
            // On parcours la structure de données Mémoire de noeud composé d'allocateur
            unsigned int j = 0;
            while (j < m_listMemoryData.size())
            {
                // On lit le premier noeud et on récupère la liste d'allocateur
                MemoryData elt = m_listMemoryData.front();
                std::list<AllocData> listAllocData = elt.getAllocList();

                // Si le nom du nom de l'allocateur à actualiser/ajouter existe dans la structure de données
                if (AllocMem == elt.getMemoryName())
                {
                    // On initialise les arguments
                    AllocData newAlloc;
                    newAlloc.setAllocatorNode(nodeID);
                    char buff[33];
                    newAlloc.setAllocatorID(itoa(qToBigEndian(*((int *)&message[p_cursor + 1])), buff, 16));

                    newAlloc.updateMaxValue(qToBigEndian(*((int *)&message[p_cursor + 5])));
                    newAlloc.setTotalMemory(qToBigEndian(*((int *)&message[p_cursor + 9])));

                    newAlloc.setAllocatorName(AllocatorIDtoName(newAlloc.getId()));
                    newAlloc.setAllocatorParent(AllocatorIDtoAttachedMemory(newAlloc.getId()));
                    listAllocData.push_back(newAlloc);

                    elt.setAllocList(listAllocData);

                    m_listMemoryData.push_back(elt);
                    m_listMemoryData.pop_front();
                }
                j++;
            }
        }
        // On se place pour la prochaine lecture
        p_cursor += 12;
    }
}


/**
 * @brief HMI::readHeapMemoryData
 * @param p_cursor
 * @param message
 */
void HMI::readHeapMemoryData(int p_cursor, std::string message)
{
    // On actualise la structure de données Mémoire : global heap
    // On parcours la partie global heap du message Platform Com, le premier octet correspond au nombre de global heap
    int initialPos = p_cursor;
    string FlagName;
    for (int j = 0; j < (int)message[initialPos]; j++)
    {
        unsigned int i = 0;
        bool nodeexist = false;
        // On parcours la structure de données Mémoire
        while (i < m_listMemoryData.size())
        {
            // On lit le premier objet de la liste
            MemoryData elt = m_listMemoryData.front();
            // On lit le nom de la heap memory à partir de son ID
            FlagName = heapMemoryIDtoName((int)message[p_cursor + 1]);
            // On cherche si le noeud est existant ou non, et si on le trouve :
            if (elt.getMemoryName() == FlagName)
            {
                // On actualise les valeurs
                elt.updateMaxUsedPlacePercent(2, qToBigEndian(*((int *)&message[p_cursor + 2])), qToBigEndian(*((int *)&message[p_cursor + 6])));
                elt.updateMaxUsedPlace(2, qToBigEndian(*((int *)&message[p_cursor + 2])));
                m_listMemoryData.push_back(elt);
                m_listMemoryData.pop_front();
                nodeexist = true;
            }
            // Sinon, on continue à parcourir la liste
            else
            {
                m_listMemoryData.push_back(elt);
                m_listMemoryData.pop_front();
            }
            i++;
        }
        // Si nouvelle heap memory
        if (nodeexist == false)
        {
            // On initialise le noeud
            MemoryData new_memoryNode;
            FlagName = heapMemoryIDtoName((int)message[p_cursor + 1]);
            new_memoryNode.setName(FlagName);
            new_memoryNode.updateMaxUsedPlacePercent(2, qToBigEndian(*((int *)&message[p_cursor + 2])), qToBigEndian(*((int *)&message[p_cursor + 6])));
            new_memoryNode.updateMaxUsedPlace(2, qToBigEndian(*((int *)&message[p_cursor + 2])));

            m_listMemoryData.push_back(new_memoryNode);
        }
        // On se place pour la prochaine lecture
        p_cursor += 9;
    }
}

/**
 * @brief HMI::readHeapLocalData
 * @param nodeID
 * @param p_cursor
 * @param message
 */
void HMI::readHeapLocalData(int nodeID, int p_cursor, std::string message)
{
    // Local Heap : noeud local
    string nodeMemoryName = "LOCAL";

    unsigned int i = 0;
    // Existance du noeud
    bool nodeexist = false;
    // On parcourt la structure de donnée
    while (i < m_listMemoryData.size())
    {
        MemoryData elt = m_listMemoryData.front();
        // Si on trouve le bon noeud
        if (elt.getNode() == QString::number(nodeID))
        {
            // On actualise les valeurs
            elt.updateMaxUsedPlacePercent(0, qToBigEndian(*((int *)&message[p_cursor])), qToBigEndian(*((int *)&message[p_cursor+4])));
            elt.updateMaxUsedPlacePercent(1, qToBigEndian(*((int *)&message[p_cursor+8])), qToBigEndian(*((int *)&message[p_cursor+12])));
            elt.updateMaxUsedPlace(0, qToBigEndian(*((int *)&message[p_cursor])));
            elt.updateMaxUsedPlace(1, qToBigEndian(*((int *)&message[p_cursor+8])));
            m_listMemoryData.push_back(elt);
            m_listMemoryData.pop_front();
            nodeexist = true;
        }
        // Sinon on passe à l'élément suivant
        else
        {
            m_listMemoryData.push_back(elt);
            m_listMemoryData.pop_front();
        }
        i++;
    }
    // Si nouvelle local heap
    if (nodeexist == false)
    {
        //On initialise le nouveau noeud
        MemoryData new_memoryNode;

        new_memoryNode.setName(nodeMemoryName);
        new_memoryNode.setHeapNode(QString::number(nodeID));
        new_memoryNode.updateMaxUsedPlacePercent(0, qToBigEndian(*((int *)&message[p_cursor])), qToBigEndian(*((int *)&message[p_cursor+4])));
        new_memoryNode.updateMaxUsedPlacePercent(1, qToBigEndian(*((int *)&message[p_cursor+8])), qToBigEndian(*((int *)&message[p_cursor+12])));
        new_memoryNode.updateMaxUsedPlace(0, qToBigEndian(*((int *)&message[p_cursor])));
        new_memoryNode.updateMaxUsedPlace(1, qToBigEndian(*((int *)&message[p_cursor+8])));

        m_listMemoryData.push_back(new_memoryNode);
    }
}


string HMI::heapMemoryIDtoName(int p_heapID)
{
    string m_heapName;
    // On parcorus la liste pour y trouver le nom associé à cette ID
    for (std::list<PlatformFile>::iterator it = m_listPfFile.begin(); it != m_listPfFile.end(); it++)
    {
        PlatformFile currentElt = *it;
        if (p_heapID == currentElt.getPFID())
        {
            m_heapName = currentElt.getPFName();
        }
    }
    return m_heapName;
}

string HMI::AllocatorIDtoName(string p_AllocID)
{
    string m_AllocName;
    // On parcorus la liste pour y trouver le nom associé à cette ID
    for (std::list<MemoryConfigFile>::iterator it = m_listMemoryConfigFile.begin(); it != m_listMemoryConfigFile.end(); it++)
    {
        MemoryConfigFile currentElt = *it;
        // l'ID lu est dépourvu du 0x
        if ("0x" + p_AllocID == currentElt.getID())
        {
            m_AllocName = currentElt.getName();
        }
    }

    return m_AllocName;
}

string HMI::AllocatorIDtoAttachedMemory(string p_AllocID)
{
    string m_AllocAttachedMemory;// = "LOCAL";
    // On parcorus la liste pour y trouver l'attached memory associé à cette ID
    for (std::list<MemoryConfigFile>::iterator it = m_listMemoryConfigFile.begin(); it != m_listMemoryConfigFile.end(); it++)
    {
        MemoryConfigFile currentElt = *it;
        if ("0x" + p_AllocID == currentElt.getID())
        {
            m_AllocAttachedMemory = currentElt.getAttachedMemory();
        }
    }

    return m_AllocAttachedMemory;
}


void HMI::copyConfPath(QString p_confPath)
{
    // On reset la liste à chaque lecture de fichier
    while (m_listMemoryConfigFile.size() != 0) m_listMemoryConfigFile.pop_front();

    // lecture du fichier Memory contenant les infos sur les allocateurs
    QFile l_configurationFile (p_confPath);
    l_configurationFile.open(QFile::ReadOnly);

    QXmlStreamReader l_configReader;

    // Go to the file beginning
    l_configurationFile.seek(0);
    l_configReader.setDevice(&l_configurationFile);

    // Get the IP value
    while (!(l_configReader.atEnd()))
    {
        MemoryConfigFile newElement;

        if(l_configReader.name().toString() == "ID")
        {
            // Une fois l'ID trouvé on initialise l'élément qu'on push dans la liste
            newElement.setID(l_configReader.readElementText().toStdString());
            l_configReader.readNextStartElement();
            newElement.setName(l_configReader.readElementText().toStdString());
            l_configReader.readNextStartElement();
            newElement.setType(l_configReader.readElementText().toStdString());
            l_configReader.readNextStartElement();
            newElement.setAttachedMemory(l_configReader.readElementText().toStdString());
            l_configReader.readNextStartElement();
            newElement.setVisibility(l_configReader.readElementText().toStdString());

            m_listMemoryConfigFile.push_back(newElement);
        }

        l_configReader.readNextStartElement();
    }
}

void HMI::getOpus3Platform(QString l_platformDir)
{
    // On reset la liste à chaque lecture de fichier
    while (m_listPfFile.size() != 0) m_listPfFile.pop_front();

    QFile l_platformFile (l_platformDir);
    l_platformFile.open(QFile::ReadOnly);

    QXmlStreamReader l_configReader;

    // Go to the file beginning
    l_platformFile.seek(0);
    l_configReader.setDevice(&l_platformFile);

    // Get the IP value
    while (!(l_configReader.atEnd()))
    {
        PlatformFile newElement;

        if(l_configReader.name().toString() == "ID")
        {
            newElement.setPFID(l_configReader.readElementText().toInt());
            l_configReader.readNextStartElement();
            newElement.setPFName(l_configReader.readElementText().toStdString());


            m_listPfFile.push_back(newElement);
        }

        l_configReader.readNextStartElement();
    }
}

